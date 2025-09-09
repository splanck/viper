# Testing

## Tolerance checks for float e2e

Floating point outputs in end-to-end tests can vary slightly. To keep tests stable, expectations are
stored in files containing lines such as:

```
EXPECT≈ 3.14 0.05
```

The `tests/e2e/support/FloatOut` helper reads program output and these expectation files and fails
if any absolute difference exceeds the specified tolerance. The CMake test driver runs this helper
after executing the sample.
