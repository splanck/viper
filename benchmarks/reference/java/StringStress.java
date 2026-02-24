/** StringStress.java — String manipulation benchmark (50K iterations).
    Equivalent to examples/il/benchmarks/string_stress.il */
public class StringStress {
    public static void main(String[] args) {
        long sum = 0;
        for (long i = 0; i < 50000; i++) {
            String t = "Hello" + " " + "World" + "!";
            sum += t.length();
        }
        System.exit((int)(sum & 0xFF));
    }
}
