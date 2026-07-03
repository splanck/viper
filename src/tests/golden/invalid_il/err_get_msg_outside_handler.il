il 0.3.0

func @main(%err:Error) -> str {
entry(%err:Error):
  %msg = err.get_msg %err
  ret %msg
}
