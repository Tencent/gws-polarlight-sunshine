# test_sunshine_api.x by Shawn Xiong

# Import Sunshine over XLang's IPC
import sunshine thru 'lrpc:31415'
import json
import time

# --- Event Handlers --------------------------------------------------------

def on_session_connect():
    # Called when a new user session connects
    print("Event: session connected")

def on_session_disconnect():
    # Called when a user session disconnects
    print("Event: session disconnected")

# Register the event handlers
sunshine.OnSessionConnect += on_session_connect
sunshine.OnSessionDisconnect += on_session_disconnect

# --- Test Sequence --------------------------------------------------------

print("=== Sunshine API Test Starting ===")

# Give the IPC layer a moment to initialize
time.sleep(0.5)

# 1) Test OnClientAction
action_name = "launch"
args_obj = {
    "args": {
        "userID": "test_user",
        "watermark": "Enable"
    }
}
json_args = json.dumps(args_obj)
print("Calling OnClientAction('${action_name}', '${json_args}', 'uuid-1234')")
ret = sunshine.OnClientAction(action_name, json_args, "uuid-1234")
print("Result: OnClientAction returned ${ret}")

# 2) Test OnQueryRuntimeInfo
query_name = "session_count"
print("Calling OnQueryRuntimeInfo('${query_name}')")
info = sunshine.OnQueryRuntimeInfo(query_name)
print("Result: runtime info for '${query_name}' = ${info}")

# 3) Test OnQueryMetrics for a couple of metrics
metric1 = "FPS"
print("Querying metric '${metric1}'")
value1 = sunshine.OnQueryMetrics(metric1)
print("Result: ${metric1} = ${value1}")

metric2 = "M.lastRtt"
print("Querying metric '${metric2}'")
value2 = sunshine.OnQueryMetrics(metric2)
print("Result: ${metric2} = ${value2}")

print("=== Sunshine API Test Completed ===")
