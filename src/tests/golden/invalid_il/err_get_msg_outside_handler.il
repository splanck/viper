il 0.2.0

func @main(%err:Error) -> str {
entry(%err:Error):
  %msg = err.get_msg %err
  ret %msg
}
