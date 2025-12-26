REM BUG-026 repro: DO WHILE with only GOSUB in body
count% = 0
DO WHILE count% < 3
    GOSUB Increment
LOOP
END

Increment:
    count% = count% + 1
RETURN

