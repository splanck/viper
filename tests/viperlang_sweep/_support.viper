module Support;

var failures: Integer = 0;

func fail(msg: String) {
    failures = failures + 1;
    Viper.Terminal.Say("FAIL: " + msg);
}

func assertTrue(cond: Boolean, msg: String) {
    if (!cond) {
        fail(msg);
    }
}

func assertFalse(cond: Boolean, msg: String) {
    if (cond) {
        fail(msg);
    }
}

func assertEqInt(actual: Integer, expected: Integer, msg: String) {
    if (actual != expected) {
        fail(msg + " (got " + toString(actual) + ", want " + toString(expected) + ")");
    }
}

func assertEqNum(actual: Number, expected: Number, msg: String) {
    if (actual != expected) {
        fail(msg + " (got " + toString(actual) + ", want " + toString(expected) + ")");
    }
}

func assertEqStr(actual: String, expected: String, msg: String) {
    if (actual != expected) {
        fail(msg + " (got \"" + actual + "\", want \"" + expected + "\")");
    }
}

func assertApprox(actual: Number, expected: Number, eps: Number, msg: String) {
    if (Viper.Math.Abs(actual - expected) > eps) {
        fail(msg + " (got " + toString(actual) + ", want " + toString(expected) + ")");
    }
}

func assertNotEmpty(actual: String, msg: String) {
    if (actual == "") {
        fail(msg);
    }
}

func assertGt(actual: Integer, threshold: Integer, msg: String) {
    if (actual <= threshold) {
        fail(msg + " (got " + toString(actual) + ", want > " + toString(threshold) + ")");
    }
}

func assertGte(actual: Integer, threshold: Integer, msg: String) {
    if (actual < threshold) {
        fail(msg + " (got " + toString(actual) + ", want >= " + toString(threshold) + ")");
    }
}

func assertLt(actual: Integer, threshold: Integer, msg: String) {
    if (actual >= threshold) {
        fail(msg + " (got " + toString(actual) + ", want < " + toString(threshold) + ")");
    }
}

func report() {
    if (failures == 0) {
        Viper.Terminal.Say("RESULT: ok");
    } else {
        Viper.Terminal.Say("RESULT: fail " + toString(failures));
    }
}
