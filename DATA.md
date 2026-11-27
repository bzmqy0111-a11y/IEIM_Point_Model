# Scientific inputs

The model uses 18 coefficient files paired with 18 EOF spatial-mode files. These 36 files are copied byte-for-byte from the existing IEIM package; their floating-point values are not refitted or recompressed.

Each spatial file stores latitude, longitude, and the spatial coefficient. Its 5,183 rows are ordered by longitude first, with latitude descending within each longitude. Latitude runs from 87.5 to -87.5 in steps of -2.5; longitude runs from -180 to 180 in steps of 5.

Coefficient files contain MODE, ICI_MEAN, ICI_STD, BETA_j and SCALE_j. The mode index identifies the corresponding spatial file. Normalization and feature scaling are applied using these parameters. Fixed period groups and quadratic ICI feature ordering are implemented in `include/ieim_model.hpp`.

`input/ici_2004_2024.txt` contains only Year, DOY and ICI for the inclusive interval 2004-01-01 through 2024-12-31, 7,671 daily records. ICI value strings are retained from the supplied daily series; the unused fractional-year column is omitted. The model's parameter training excluded 2009, 2014 and 2018; retaining their ICI records is useful for predictions and does not imply they were used for fitting.

Use `DATA_MANIFEST.json` to verify the exact files supplied with this package. No data files from before 2004 are included.
