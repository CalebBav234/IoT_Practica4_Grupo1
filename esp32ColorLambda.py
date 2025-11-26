import json
import boto3
from datetime import datetime, timezone
from boto3.dynamodb.conditions import Key, Attr
from dateutil import parser
import time

# DynamoDB tables
dynamo = boto3.resource('dynamodb', region_name='us-east-1')
user_table = dynamo.Table('UserThings')
events_table = dynamo.Table('ColorControllerEvents')

# IoT client for Shadow updates
iot = boto3.client('iot-data', region_name='us-east-2')

# Pill colors
DISPENSE_ANGLES = {
    "WHITE": 0, "CREAM": 30, "BROWN": 60,
    "RED": 90, "BLUE": 120, "GREEN": 150, "OTHER": 180
}
VALID_COLORS = DISPENSE_ANGLES.keys()

# --- Main Entry Point ---
def lambda_handler(event, context):
    print(f"Received event: {json.dumps(event)}")
    
    # Detect event type: IoT Rule or Alexa
    if 'thing_name' in event and 'event_timestamp' in event:
        # IoT Rule event from Rule 2
        return handle_iot_rule_event(event, context)
    elif 'session' in event and 'request' in event:
        # Alexa event
        return handle_alexa_event(event, context)
    else:
        print("Unknown event type")
        return {
            'statusCode': 400,
            'body': json.dumps('Unknown event type')
        }


# ============= IoT Rule Handler =============
def handle_iot_rule_event(event, context):
    """
    Handle events from IoT Rule 2 (scheduled pill monitoring).
    
    CRITICAL: This does NOT update desired state (no loop risk!)
    It only logs that scheduled time was reached.
    """
    print(f"IoT Rule event: {json.dumps(event)}")
    
    thing_name = event.get('thing_name')
    pill_hour = event.get('pill_hour')
    pill_minute = event.get('pill_minute')
    pill_name = event.get('pill_name', 'UNKNOWN')
    last_dispense = event.get('last_dispense', 0)
    
    if pill_hour is None or pill_minute is None:
        print("No scheduled time in event")
        return {'statusCode': 200, 'body': 'No schedule'}
    
    # Convert to int
    pill_hour = int(pill_hour)
    pill_minute = int(pill_minute)
    last_dispense = int(last_dispense) if last_dispense else 0
    
    # Get current time
    now = datetime.now(timezone.utc)
    current_hour = now.hour
    current_minute = now.minute
    
    # Check if last dispense was today
    if last_dispense > 0:
        last_dispense_dt = datetime.fromtimestamp(last_dispense, tz=timezone.utc)
        if last_dispense_dt.date() == now.date():
            print(f"Already dispensed today at {last_dispense_dt}")
            return {'statusCode': 200, 'body': 'Already dispensed today'}
    
    # Check if it's within 1 minute of scheduled time
    time_diff = abs((current_hour * 60 + current_minute) - (pill_hour * 60 + pill_minute))
    
    if time_diff <= 1:
        print(f"Scheduled time reached: {pill_name} at {pill_hour}:{pill_minute}")
        
        # Log to DynamoDB (monitoring only, no action)
        events_table.put_item(Item={
            'command_id': int(time.time() * 1000),
            'timestamp': int(now.timestamp()),
            'thing_name': thing_name,
            'pill_name': pill_name,
            'pill_hour': pill_hour,
            'pill_minute': pill_minute,
            'user_id': 'SYSTEM',
            'event_type': 'scheduled_time_reached',
            'reported': {
                'last_dispense': last_dispense,
                'check_time': now.isoformat()
            }
        })
        
        return {
            'statusCode': 200,
            'body': json.dumps('Scheduled time logged')
        }
    
    print(f"Not pill time: {current_hour}:{current_minute} vs {pill_hour}:{pill_minute}")
    return {'statusCode': 200, 'body': 'Not pill time'}


# ============= Alexa Handler =============
def handle_alexa_event(event, context):
    """Handle Alexa skill requests"""
    try:
        user_id = event['session']['user']['userId']
        print(f"Alexa event - User ID: {user_id}")

        device = get_user_device(user_id)
        if not device:
            return build_response("No smart pill dispensers are configured for your account.", end_session=True)
        
        thing_name = device['thing_name']
        friendly_name = device.get('description', 'pill dispenser')

        request_type = event['request']['type']

        if request_type == "LaunchRequest":
            return build_response(
                f"Welcome to {friendly_name}. You can schedule pills, dispense one now, or ask for your next or last pill."
            )

        elif request_type == "IntentRequest":
            intent = event['request']['intent']
            intent_name = intent['name']

            if intent_name == "SetPillScheduleIntent":
                pill_name_slot = intent['slots'].get('PillName', {})
                if not pill_name_slot.get('value'):
                    return build_response("I didn't catch the pill name. Please try again.")
                pill_name = pill_name_slot['value']
                session_attrs = {"pill_name": pill_name}
                return build_response_with_session(
                    f"You said {pill_name}. What color is the pill and at what time should I schedule it?",
                    session_attrs=session_attrs
                )

            elif intent_name == "SetPillTimeIntent":
                pill_name = event['session'].get('attributes', {}).get('pill_name')
                color_slot = intent['slots'].get('Color', {})
                time_slot = intent['slots'].get('Time', {})

                if not pill_name:
                    return build_response("I lost track of which pill we were scheduling. Please start over.")

                if not color_slot.get('value'):
                    return build_response_with_session("What color is the pill?", {"pill_name": pill_name})
                if not time_slot.get('value'):
                    return build_response_with_session("At what time should I schedule it?", {"pill_name": pill_name})

                color = color_slot['value'].upper()
                if color not in VALID_COLORS:
                    valid_colors_list = ", ".join(VALID_COLORS)
                    return build_response_with_session(
                        f"{color} is not valid. Valid colors: {valid_colors_list}.", {"pill_name": pill_name}
                    )

                try:
                    time_str = time_slot['value']
                    dt = parser.parse(time_str)
                    hour, minute = dt.hour, dt.minute
                except:
                    return build_response_with_session(
                        "I couldn't understand that time. Please say like 8 AM or 2:30 PM.", {"pill_name": pill_name}
                    )

                command_id = int(time.time() * 1000)

                shadow_payload = {
                    "state": {
                        "desired": {
                            "pill_name": pill_name,
                            "color": color,
                            "pill_hour": hour,
                            "pill_minute": minute,
                            "buzzer_enabled": True,
                            "dispense_now": False,
                            "command_id": command_id
                        }
                    }
                }
                iot.update_thing_shadow(thingName=thing_name, payload=json.dumps(shadow_payload))

                events_table.put_item(Item={
                    'command_id': command_id,
                    'timestamp': int(datetime.now(timezone.utc).timestamp()),
                    'thing_name': thing_name,
                    'pill_name': pill_name,
                    'pill_hour': hour,
                    'pill_minute': minute,
                    'color': color,
                    'buzzer_enabled': True,
                    'user_id': user_id,
                    'event_type': 'schedule_update',
                    'reported': {}
                })

                time_12h = dt.strftime('%I:%M %p')
                return build_response(f"Scheduled {color.lower()} pill {pill_name} at {time_12h}. What else can I help you with?")

            elif intent_name == "DispensePillIntent":
                pill_name_slot = intent['slots'].get('PillName', {})
                if not pill_name_slot.get('value'):
                    return build_response("I didn't catch the pill name. Which pill should I dispense?")
                return handle_dispense(user_id, thing_name, pill_name_slot['value'])

            elif intent_name == "GetCurrentPillIntent":
                return get_next_pill(user_id)

            elif intent_name == "GetLastDispensedPillIntent":
                return get_last_dispensed(user_id)

            elif intent_name == "AMAZON.HelpIntent":
                return build_response("You can schedule pills, dispense them, or ask about next or last pill.")
            elif intent_name in ["AMAZON.StopIntent", "AMAZON.CancelIntent", "AMAZON.NavigateHomeIntent"]:
                return build_response("Goodbye!", end_session=True)
            elif intent_name == "AMAZON.FallbackIntent":
                return build_response("I didn't understand that. You can schedule pills, dispense, or ask about next/last pill.")

        return build_response("I didn't understand that. What would you like to do?")

    except Exception as e:
        print(f"Alexa handler error: {e}")
        import traceback
        traceback.print_exc()
        return build_response("There was an error processing your request.", end_session=True)


# ============= Helper Functions =============

def get_user_device(user_id):
    resp = user_table.query(KeyConditionExpression=Key('user_id').eq(user_id))
    if not resp['Items']:
        return None
    return resp['Items'][0]


def handle_dispense(user_id, thing_name, pill_name):
    resp = events_table.scan(
        FilterExpression=Attr('user_id').eq(user_id) & Attr('event_type').eq('schedule_update')
    )
    pill_color = "UNKNOWN"
    found = False
    for item in resp.get('Items', []):
        if item.get('pill_name', '').lower() == pill_name.lower():
            found = True
            pill_color = item.get('color', 'UNKNOWN')
            break
    
    if not found:
        return build_response(f"Pill {pill_name} not found in schedules. Please schedule it first.")

    command_id = int(time.time() * 1000)
    
    shadow_payload = {
        "state": {
            "desired": {
                "pill_name": pill_name,
                "color": pill_color,
                "dispense_now": True,
                "command_id": command_id
            }
        }
    }
    iot.update_thing_shadow(thingName=thing_name, payload=json.dumps(shadow_payload))

    events_table.put_item(Item={
        'command_id': command_id,
        'timestamp': int(datetime.now(timezone.utc).timestamp()),
        'thing_name': thing_name,
        'pill_name': pill_name,
        'color': pill_color,
        'user_id': user_id,
        'event_type': 'dispense',
        'reported': {}
    })

    return build_response(f"Dispensing {pill_name} now. What else can I help you with?")


def get_next_pill(user_id):
    resp = events_table.scan(
        FilterExpression=Attr('user_id').eq(user_id) & Attr('event_type').eq('schedule_update')
    )
    items = resp.get('Items', [])
    if not items:
        return build_response("No pills scheduled. Would you like to schedule one?")
    
    now = datetime.now()
    current_minutes = now.hour * 60 + now.minute
    next_pill = None
    min_diff = float('inf')
    
    for item in items:
        h = int(item.get('pill_hour', 0))
        m = int(item.get('pill_minute', 0))
        pill_minutes = h * 60 + m
        
        if pill_minutes >= current_minutes:
            diff = pill_minutes - current_minutes
        else:
            diff = (24 * 60 - current_minutes) + pill_minutes
        
        if diff < min_diff:
            min_diff = diff
            next_pill = item
    
    if next_pill:
        dt = now.replace(hour=int(next_pill['pill_hour']), minute=int(next_pill['pill_minute']))
        color = next_pill.get('color', 'UNKNOWN').lower()
        return build_response(
            f"Your next scheduled pill is {color} {next_pill['pill_name']} at {dt.strftime('%I:%M %p')}."
        )
    
    return build_response("No upcoming pills found.")


def get_last_dispensed(user_id):
    resp = events_table.scan(
        FilterExpression=Attr('user_id').eq(user_id) & Attr('event_type').eq('dispense')
    )
    items = sorted(resp.get('Items', []), key=lambda x: int(x['timestamp']), reverse=True)
    
    if items:
        last = items[0]
        dt = datetime.fromtimestamp(int(last['timestamp']), tz=timezone.utc)
        color = last.get('color', 'UNKNOWN').lower()
        return build_response(
            f"The last dispensed pill was {color} {last['pill_name']} at {dt.strftime('%I:%M %p')}."
        )
    
    return build_response("No pills have been dispensed yet.")


def build_response(text, end_session=False):
    return {
        "version": "1.0",
        "sessionAttributes": {},
        "response": {
            "outputSpeech": {"type": "PlainText", "text": text},
            "shouldEndSession": end_session
        }
    }


def build_response_with_session(text, session_attrs=None, end_session=False):
    return {
        "version": "1.0",
        "sessionAttributes": session_attrs or {},
        "response": {
            "outputSpeech": {"type": "PlainText", "text": text},
            "shouldEndSession": end_session
        }
    }