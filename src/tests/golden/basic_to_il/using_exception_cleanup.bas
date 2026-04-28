CLASS Resource
  SUB DESTROY()
  END SUB
END CLASS

USING R AS Resource = NEW Resource()
  PRINT "body"
END USING
END
