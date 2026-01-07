' Minimal ON ERROR program that used to trip IL verifier with
' an "empty block" for a preallocated unlabeled block.

ON ERROR GOTO Handler

END

Handler:
  END

