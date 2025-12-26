CLASS C
  v AS INTEGER
  SUB A() : me.v = 1 : END SUB
  SUB B() : ME.v = ME.v + 1 : END SUB
  SUB Show() : Print Me.v : END SUB
END CLASS
DIM c AS C : c = NEW C() : c.A() : c.B() : c.Show() : END
