import json
import boto3

# Lambda client for cross-region invocation
lambda_client = boto3.client('lambda', region_name='us-east-1')

def lambda_handler(event, context):
    """
    Proxy Lambda in us-east-2 that forwards IoT Rule events 
    to main Lambda in us-east-1.
    This is needed because IoT Rules can only invoke resources in same region.
    """
    print(f"Proxy received event: {json.dumps(event)}")
    
    try:
        # Invoke the main Lambda in us-east-1
        response = lambda_client.invoke(
            FunctionName='esp32ColorLambda',
            InvocationType='RequestResponse',
            Payload=json.dumps(event)
        )
        
        # Parse response
        response_payload = json.loads(response['Payload'].read())
        print(f"Main Lambda response: {json.dumps(response_payload)}")
        
        return response_payload
        
    except Exception as e:
        print(f"Error invoking main Lambda: {e}")
        import traceback
        traceback.print_exc()
        return {
            'statusCode': 500,
            'body': json.dumps(f'Proxy error: {str(e)}')
        }