# Layouts the spec reader knows

Short, made-up excerpts in the shape of each exam board's specifications (their headings, numbering and order, not
their text), with the tree Streamline reads from each (`.expected`). CI checks every change to the reader against
them (`.github/workflows/specs.yml`), alongside the boards' real documents.

To add one: write the excerpt, then `spec_test layout.txt > layout.expected` and check the tree by eye.
