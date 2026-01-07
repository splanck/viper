' mixed_labels.bas — GOTO must still jump to explicit line labels
PRINT "start"
GOTO 100
PRINT "skipped unlabeled"
100 PRINT "landed on 100"
END
