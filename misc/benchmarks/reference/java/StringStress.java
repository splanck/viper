/** StringStress.java — String manipulation benchmark (500K iterations).
    Equivalent to examples/il/benchmarks/string_stress.il */
public class StringStress {
    public static void main(String[] args) {
        long n = 500000 + args.length;
        long sum = 0;
        for (long i = 0; i < n; i++) {
            String t = "Hello" + " " + "World" + "!";
            sum += t.length();
        }
        System.exit((int)(sum & 0xFF));
    }
}
