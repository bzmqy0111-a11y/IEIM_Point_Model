# Validation record

Validated on Windows x64 (MinGW GCC 8.1) and Linux/WSL (GCC 11.4).

- Direct build scripts and CMake builds passed on both platforms.
- CTest passed on both platforms.
- 600 hourly point values matched independently frozen IEIM GIM references within 0.055 TECU: 0.05 from reference quantization plus 0.005 from final two-decimal output rounding.
- 120 fractional-UT point values matched frozen references based on exported IEIM LT coefficients after formatting both reference and CLI VTEC to two decimal places.
- Modes 1, 10 and 18, exact grid nodes, four-corner interpolation, both longitude endpoints and both latitude limits were covered.
- Tests covered UT 0 and 24, fractional hours, leap days, year rollover, missing/supplied next-day ICI, conflicting daily ICI, invalid dates/coordinates/UT, BOM/comments, file output and the full ICI date coverage.
- A compiled Windows main-project C++ example agreed with the CLI at UT12.5.
- All 36 scientific model files are byte-identical to the existing IEIM package.
- Existing IEIM package files, including its code, model inputs, examples and user outputs, remained unchanged.

CLI and example VTEC text values have exactly two decimal places. The model library retains unrounded double-precision results. All three reference cases and all test checks are shipped in `tests/`; the original development workspace is not required to run the tests. The included GitHub Actions workflow has been prepared but has not been run on GitHub.
