il 0.2.0
func @cast_checks(%si:i64, %ui:i64, %fp:f64) -> void {
entry(%si:i64, %ui:i64, %fp:f64):
  %t0 = cast.fp_to_si.rte.chk %fp
  %t1 = cast.fp_to_ui.rte.chk %fp
  %t2 = cast.si_narrow.chk %si
  %t3 = cast.ui_narrow.chk %ui
  %t4 = cast.si_to_fp %si
  %t5 = cast.ui_to_fp %ui
  ret
}
