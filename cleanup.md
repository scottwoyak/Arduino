# Code Cleanup Checklist

Use this checklist when performing a code cleanup pass to ensure all areas are evaluated against the specific rules defined in `copilot-instructions.md`. Refer to that file for the exact definitions and examples.

## 1. Safety & Preservation
- [ ] **Comment Preservation**: Check rules regarding existing inline comments and commented-out code.
- [ ] **Existing Functionality**: Verify changes do not alter intended logic.
- [ ] **Section Dividers**: Check rules regarding existing division comments.

## 2. File Organization & Structure
- [ ] **.ino Structure**: Verify code block ordering matches the defined template.
- [ ] **Include Ordering**: Order includes according to the specified categories.
- [ ] **Include Auditing**: Review for missing, duplicate, or unused headers.

## 3. Documentation (Comments)
- [ ] **Sketch Docs**: Verify top-of-file documentation follows the sketch-specific format.
- [ ] **XML Docs `///`**: Ensure classes, properties, enums, and methods are documented appropriately.
- [ ] **XML Wrapping & Spacing**: Verify line spacing around `///` blocks matches the rules.
- [ ] **Method Params/Returns**: Ensure all required tags are present for functions.
- [ ] **Enums & Fields**: Apply documentation rules specific to parent declarations vs self-evident members.

## 4. Naming Conventions & Modifiers
- [ ] **Privates/Protected**: Review prefix and declaration order rules.
- [ ] **Array Counts**: Review the required naming convention for array count constants.
- [ ] **Timing Units**: Check that timing-related variables use the approved names and suffixes.
- [ ] **Pointers vs Refs**: Check the preference rule for parameters and members.

## 5. Types, Constants & Values
- [ ] **Const / Constexpr**: Review the rules for when to use each keyword.
- [ ] **Integer Types**: Ensure the approved integer types are used.
- [ ] **Magic Numbers**: Ensure integers/floats follow the extraction rules.
- [ ] **String Literals**: Check the expected declaration style for string constants.

## 6. Optimization, Timing & Processing
- [ ] **Initialization**: Follow the designated precedence for member initialization.
- [ ] **Timing Mechanics**: Review rules surrounding delay logic and elapsed time calculation.
- [ ] **Templates / Overloads**: Check the preference regarding template meta-programming versus explicit overloads.
- [ ] **Platform Target**: Review rules for ESP32-S3 specific optimizations.
- [ ] **Zero/Guard Checks**: Review the conditions under which guard checks should or shouldn't be removed.
- [ ] **ASSERT Placement**: Verify ASSERT statements appear before all early-return guard clauses and other statements in a function, so invariants are checked before any other work (including guard-clause early returns). For every run of one or more consecutive ASSERTs immediately followed by non-ASSERT code (including runs that occur mid-function, not just at the top), verify a blank line separates the last ASSERT from the next line of code.

## 7. Display, Strings & Formatting
- [ ] **Serial Output**: Ensure the correct serial initialization and table classes are used.
- [ ] **Render Flicker**: Review the rule regarding screen clearing.
- [ ] **Text Sizing**: Review the text sizing rule and constant usage.
- [ ] **Value Print Formats**: Follow the rules for numeric values, formats, and ad-hoc concatenation.
- [ ] **Inline Formatters**: Check where `Format` objects should be instantiated.
- [ ] **Display Tables**: Review rules for displaying rows and headers.

## 8. General Code Quality
- [ ] **Redundant Code**: Scan for unused elements and variable shadowing.
- [ ] **Redundant Casts**: Follow the instructions regarding explicit versus implicit conversions.
- [ ] **DRY Principles**: Extract repeated code and setup logic according to guidelines.
- [ ] **Operator Preference**: Check the rule for increment operations.
- [ ] **Formatting Consistency**: Ensure you defer to `.editorconfig` for whitespace and brace rules.
- [ ] **Default-Matching Arguments**: Check calls with optional/default-valued parameters and remove any explicit argument that matches the parameter's default value.
