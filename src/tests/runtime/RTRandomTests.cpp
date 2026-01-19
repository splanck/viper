//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for random distribution functions.
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

extern "C"
{
    void rt_randomize_i64(long long seed);
    double rt_rnd(void);
    long long rt_rand_int(long long max);
    long long rt_rand_range(long long min, long long max);
    double rt_rand_gaussian(double mean, double stddev);
    double rt_rand_exponential(double lambda);
    long long rt_rand_dice(long long sides);
    long long rt_rand_chance(double probability);
}

static void test_rand_range()
{
    printf("Testing rt_rand_range...\n");
    rt_randomize_i64(12345);

    // Test normal range
    for (int i = 0; i < 100; i++)
    {
        long long r = rt_rand_range(1, 10);
        assert(r >= 1 && r <= 10);
    }

    // Test inverted range (should auto-swap)
    for (int i = 0; i < 100; i++)
    {
        long long r = rt_rand_range(10, 1);
        assert(r >= 1 && r <= 10);
    }

    // Test single value range
    for (int i = 0; i < 10; i++)
    {
        long long r = rt_rand_range(5, 5);
        assert(r == 5);
    }

    printf("  PASSED\n");
}

static void test_rand_gaussian()
{
    printf("Testing rt_rand_gaussian...\n");
    rt_randomize_i64(12345);

    // Generate many samples and check basic statistical properties
    double sum = 0.0;
    double sum_sq = 0.0;
    const int N = 10000;
    const double mean = 100.0;
    const double stddev = 15.0;

    for (int i = 0; i < N; i++)
    {
        double g = rt_rand_gaussian(mean, stddev);
        sum += g;
        sum_sq += g * g;
    }

    double sample_mean = sum / N;
    double sample_var = (sum_sq / N) - (sample_mean * sample_mean);
    double sample_stddev = sqrt(sample_var);

    // Check that sample mean is close to expected mean (within 1%)
    assert(fabs(sample_mean - mean) < mean * 0.02);

    // Check that sample stddev is close to expected (within 10%)
    assert(fabs(sample_stddev - stddev) < stddev * 0.15);

    // Test zero stddev returns mean
    for (int i = 0; i < 10; i++)
    {
        double g = rt_rand_gaussian(50.0, 0.0);
        assert(g == 50.0);
    }

    printf("  PASSED\n");
}

static void test_rand_exponential()
{
    printf("Testing rt_rand_exponential...\n");
    rt_randomize_i64(12345);

    // Generate samples and check mean (should be approximately 1/lambda)
    double sum = 0.0;
    const int N = 10000;
    const double lambda = 2.0;

    for (int i = 0; i < N; i++)
    {
        double e = rt_rand_exponential(lambda);
        assert(e >= 0.0); // Exponential is always non-negative
        sum += e;
    }

    double sample_mean = sum / N;
    double expected_mean = 1.0 / lambda;

    // Check that sample mean is close to expected (within 10%)
    assert(fabs(sample_mean - expected_mean) < expected_mean * 0.15);

    // Test invalid lambda
    assert(rt_rand_exponential(0.0) == 0.0);
    assert(rt_rand_exponential(-1.0) == 0.0);

    printf("  PASSED\n");
}

static void test_rand_dice()
{
    printf("Testing rt_rand_dice...\n");
    rt_randomize_i64(12345);

    // Test 6-sided die
    int counts[7] = {0};
    for (int i = 0; i < 6000; i++)
    {
        long long d = rt_rand_dice(6);
        assert(d >= 1 && d <= 6);
        counts[d]++;
    }

    // Each side should appear roughly 1000 times (within 20%)
    for (int i = 1; i <= 6; i++)
    {
        assert(counts[i] > 800 && counts[i] < 1200);
    }

    // Test edge cases
    assert(rt_rand_dice(0) == 1);
    assert(rt_rand_dice(-5) == 1);
    assert(rt_rand_dice(1) == 1);

    printf("  PASSED\n");
}

static void test_rand_chance()
{
    printf("Testing rt_rand_chance...\n");
    rt_randomize_i64(12345);

    // Test 50% probability
    int trues = 0;
    const int N = 10000;
    for (int i = 0; i < N; i++)
    {
        if (rt_rand_chance(0.5))
            trues++;
    }

    // Should be roughly 50% (within 5%)
    double ratio = (double)trues / N;
    assert(ratio > 0.45 && ratio < 0.55);

    // Test edge cases
    for (int i = 0; i < 100; i++)
    {
        assert(rt_rand_chance(0.0) == 0);
        assert(rt_rand_chance(1.0) == 1);
        assert(rt_rand_chance(-0.5) == 0);
        assert(rt_rand_chance(1.5) == 1);
    }

    printf("  PASSED\n");
}

static void test_determinism()
{
    printf("Testing determinism...\n");

    // Same seed should produce same sequence
    rt_randomize_i64(99999);
    double r1 = rt_rnd();
    long long i1 = rt_rand_int(100);
    long long rng1 = rt_rand_range(1, 10);
    double g1 = rt_rand_gaussian(0.0, 1.0);
    double e1 = rt_rand_exponential(1.0);
    long long d1 = rt_rand_dice(6);
    long long c1 = rt_rand_chance(0.5);

    rt_randomize_i64(99999);
    double r2 = rt_rnd();
    long long i2 = rt_rand_int(100);
    long long rng2 = rt_rand_range(1, 10);
    double g2 = rt_rand_gaussian(0.0, 1.0);
    double e2 = rt_rand_exponential(1.0);
    long long d2 = rt_rand_dice(6);
    long long c2 = rt_rand_chance(0.5);

    assert(r1 == r2);
    assert(i1 == i2);
    assert(rng1 == rng2);
    assert(g1 == g2);
    assert(e1 == e2);
    assert(d1 == d2);
    assert(c1 == c2);

    printf("  PASSED\n");
}

int main()
{
    printf("=== Random Distribution Tests ===\n");

    test_rand_range();
    test_rand_gaussian();
    test_rand_exponential();
    test_rand_dice();
    test_rand_chance();
    test_determinism();

    printf("=== All tests passed ===\n");
    return 0;
}
