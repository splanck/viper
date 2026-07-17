' API Audit: Zanna.Network.SmtpClient (BASIC)
PRINT "=== API Audit: Zanna.Network.SmtpClient ==="
PRINT "NOTE: SMTP sends require a real mail server. This demo checks the API surface only."

DIM smtp AS OBJECT
smtp = Zanna.Network.SmtpClient.New("mail.example.test", 587)
smtp.SetTls(1)
smtp.SetAuth("user@example.test", "password")

IF 0 THEN
    DIM plain AS OBJECT
    plain = smtp.SendResult("sender@example.test", "dest@example.test", "Subject", "Body")
    PRINT plain.IsErr

    DIM html AS OBJECT
    html = smtp.SendHtmlResult("sender@example.test", "dest@example.test", "Subject", "<p>Body</p>")
    PRINT html.IsErr
END IF

smtp.Close()
PRINT "=== SmtpClient Audit Complete ==="
END
