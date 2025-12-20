module SimpleTest;

entity Counter {
    Integer count;

    func init(c: Integer) {
        count = c;
    }

    func getCount() -> Integer {
        return count;
    }
}
