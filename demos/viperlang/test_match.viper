module TestMatch;

func matchTest(x: Integer) -> Integer {
    match x {
        1 => return 10;
        2 => return 20;
        _ => return 0;
    }
    return 99;
}
