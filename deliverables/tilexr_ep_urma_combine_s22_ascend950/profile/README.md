# S22 Profile Reports

- [BS32 interactive report](bs32/combine_report.html): includes total-only,
  coarse, and fine captures. The timing headline is 41.126 us.
- [BS128 interactive report](bs128/combine_report.html): coarse/fine
  attribution report. Its 89.431 us headline is an attribution-profile value;
  use the strict100 result, 94,889 cycles, for production comparison.
- `strict100/`: canonical 100-round raw-cycle summaries.

Start gate is collapsed from proportional views but retained in report metadata.
The raw trace directories are intentionally excluded from this delivery to keep
the repository compact; generated CSV files retain the paired-launch and
per-core summaries used by the HTML report.
