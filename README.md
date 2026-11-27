# IEIM Point Model

[GitHub repository](https://github.com/bzmqy0111-a11y/IEIM_Point_Model)

A standalone C++17 IEIM model that calculates vertical total electron content (VTEC) at a geographic point. Input is **Year, DOY, ICI, Longitude, Latitude**, with an optional **UT** hour. Output is floating-point **TECU**, not IONEX. One TECU is 10^16 electrons per square metre.

## Quick start

From this folder, build and run on Linux/WSL:

```bash
bash build.sh
./reconstruct 2024 1 40.7 116.4 39.9 12.5
```

Windows PowerShell (configure MinGW g++ first to rebuild):

```powershell
.\build.bat
.\reconstruct.exe 2024 1 40.7 116.4 39.9 12.5
```

The last argument is UT in decimal hours: `12.5` means 12:30 UT. This command returns a header and one result row. Omitting UT returns 25 rows for UT 0 through 24. Coordinates are in degrees, positive east and north.

The equivalent named-argument command is:

```bash
./reconstruct --year 2024 --doy 1 --icivalue 40.7 --lon 116.4 --lat 39.9 --ut 12.5
```

Output columns are tab-separated:

```text
Year  DOY  Hour_UT  ICI  Longitude  Latitude  VTEC_TECU
```

`VTEC_TECU` is the calculated vertical TEC, not slant TEC. Text output rounds this column to exactly two decimal places (0.01 TECU). Calculations and the C++ API retain double precision internally; formatting does not change the model or imply physical accuracy of 0.01 TECU.

## Files and batch input

- `include/ieim_model.hpp`: reusable model, time conversion, and point interpolation.
- `src/main.cpp`: command-line interface and tab-separated output.
- `data/coeff_01.txt` through `coeff_18.txt`: paired model regression and normalization parameters.
- `data/spatial_01.txt` through `spatial_18.txt`: 71 by 73 EOF spatial modes.
- `input/ici_2004_2024.txt`: three-column daily ICI reference series; 7,671 days, 2004-01-01 through 2024-12-31. Earlier years are excluded.
- `examples/points_at_ut.txt`: six-column point requests, one specified UT per row.
- `examples/points.txt`: five-column requests, each producing 25 hourly samples.
- `examples/call_from_project.cpp`: direct C++ library use without a shell command.
- `tests/`: standalone numerical references and regression tests.
- `DATA_MANIFEST.json`: SHA-256 checksums of the supplied scientific inputs.

A six-column request file looks like:

```text
Year DOY ICI Longitude Latitude UT
2024 1 40.7 116.4 39.9 12.5
2024 60 65.0 -75.2 40.1 8.25
```

```bash
./reconstruct --input examples/points_at_ut.txt
./reconstruct --input examples/points_at_ut.txt --output tec.tsv
./reconstruct --input examples/points.txt --modes 10 --output hourly.tsv
```

Whitespace separates fields. A `Year`/`YEAR` header, blank lines, `#`/`%` comments and a first-line UTF-8 BOM are supported. Each data row must have five or six fields. The three-column ICI reference file is **not** a point request file: select its daily ICI and add longitude/latitude, plus UT if needed. The reference series is never loaded implicitly. Example coordinates and ICI values illustrate the interface and should be replaced with your application inputs.

Results follow input order. Repeated requests at the same point/day are permitted, including requests for different UT hours, but conflicting ICI values for that point/day are rejected. Both the five-column and six-column formats may occur in one file.

## Options and validation

| Option | Description |
|---|---|
| `--year Y --doy D --icivalue I --lon X --lat Y` | One point, as an alternative to five positional arguments |
| `--ut H` | Optional UT in decimal hours, from 0 through 24 inclusive; fractions are supported |
| `--input FILE` | Five-/six-column batch input; cannot be mixed with single-point arguments |
| `--modes N` | Sum the first N modes, 1 through 18; default 18 |
| `--data-dir DIR` | Coefficients and spatial modes; default `data/` beside the executable |
| `--output FILE` | Write the table to a new file; default standard output |
| `--help` | Print help |

Explicit relative paths are resolved against the process working directory. For calls from another project, use absolute `--data-dir` and `--output` paths when appropriate. Existing output files are rejected; select a new filename. The parent output directory must already exist. Errors go to standard error and return a nonzero status; successful calls return zero.

Year/DOY must form a valid date. ICI must be finite and nonnegative. Longitude is restricted to [-180, 180] degrees, latitude to [-87.5, 87.5], matching the supplied grid. UT must be finite and within [0, 24]. Values outside these ranges are rejected; no extrapolation to the poles is performed.

## Calculation and time convention

The model uses the fixed IEIM parameters and the first 18 spatial modes by default. No training is performed. Each selected mode predicts LT samples at hours 0 through 24, using quadratic normalized ICI terms and its fixed seasonal/local-time basis. The first TC is exponentiated; later TCs are linear. No mean field is added.

For each requested UT, the program calculates VTEC at the four surrounding regular grid nodes. At each node, `LT = (UT + node_longitude/15) mod 24`, and TC is linearly interpolated between its surrounding integer LT samples, including the LT24 endpoint. Each node sums the selected `spatial_mode * TC` terms and clips negative VTEC to zero. The point value is then obtained by bilinear interpolation in longitude and latitude:

```text
V = (1-wx)*(1-wy)*V_north_west + wx*(1-wy)*V_north_east
  + (1-wx)*wy*V_south_west + wx*wy*V_south_east
```

The spatial grid has 5-degree longitude and 2.5-degree latitude spacing, including both longitude endpoints. Exact grid coordinates return their node value. No IONEX scaling, 0.1-TECU integer encoding, missing-value sentinel, or full-map output is used. Only the final text VTEC is rounded to two decimal places. Fractional UT is evaluated through the node LT conversion above; it is not obtained by interpolating the final hourly point results.

UT24 means the next calendar day's 00:00. In a batch, a next-day ICI at the **same coordinates** is used when available, even if that row requests a different UT hour. Otherwise the current ICI is carried forward and the model is evaluated for the next date. This policy handles leap days and year rollover. Consequently, a day's UT24 and the following day's UT00 agree when both records are supplied. The output's Year/DOY identify the requested day; Hour_UT=24 labels its next-day endpoint.

Only the 2004-2024 ICI reference series is shipped. The executable accepts other valid years when an ICI is supplied, but this is not an accuracy guarantee for those dates. Spatial modes and coefficients must always be used as a matched set.

## Use in a larger C++ project

The model is header-only and has no external numerical-library dependencies:

```cpp
#include "ieim_model.hpp"

ieim::Model model("/absolute/path/to/data", 18); // Load once, then reuse.
ieim::Request point{2024, 1, 40.7, 116.4, 39.9};
double tecu = model.predict_at(point, 12.5);
auto hourly_tecu = model.predict(point); // 25 samples, UT 0..24.
// Optional next-day ICI (used at UT24):
double endpoint = model.predict_at(point, 24.0, 65.0);
```

Add `include/` to the main project's header search path and compile as C++17. `examples/call_from_project.cpp` is a complete example. CLI calls via `system()` are also possible; for Windows CMD, for example:

```bat
cd /d "D:\path with spaces\IEIM_Point_Model" && reconstruct.exe 2024 1 40.7 116.4 39.9 12.5
```

## CMake and tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For a CMake-built program in `build/`, specify `--data-dir data` when running from the repository root. Windows multi-configuration generators normally put the program in `build/Release/`. The simple build scripts place it in the repository root instead.

The test suite uses only Python's standard library:

```bash
python3 tests/test_point_model.py --exe ./reconstruct
```

On Windows, use `python -X utf8 tests/test_point_model.py --exe reconstruct.exe`. Tests compare 600 hourly values against frozen IEIM GIM references (tolerance 0.055 TECU: 0.05 from reference quantization plus 0.005 from final text rounding) and 120 fractional-UT values against references calculated from exported IEIM LT coefficients. They also cover bilinear interpolation, grid boundaries, leap/year transitions, next-day ICI, input validation and the ICI date range.

## Authors and contact

| Author | Role | Email |
|---|---|---|
| Qinghao Zhu | First author | [bzmqy0111@buaa.edu.cn](mailto:bzmqy0111@buaa.edu.cn) |
| Cheng Wang | Corresponding author | [acheng@buaa.edu.cn](mailto:acheng@buaa.edu.cn) |

## Repository and license

Build products are ignored by `.gitignore`; source, model data, documentation and tests are prepared for a GitHub repository. This project is distributed under the [MIT License](LICENSE). The copyright holders are Qinghao Zhu and Cheng Wang, identified by `Copyright (c) 2026 Qinghao Zhu and Cheng Wang`. Keep the standard permission, notice-retention and warranty-disclaimer paragraphs unchanged. The license permits use, modification and redistribution, including commercial use, provided the copyright and permission notices are retained.
