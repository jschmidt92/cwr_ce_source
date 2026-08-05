# Scripting

- [JSON Commands](json.md)
- [Database and Cache](database.md)
- [Scripting Commands](commands.md)
- [Script Event System](event-system.md)

## SQS-Style Scripts

Scripts that use SQS flow control such as labels (`#label`), conditional jumps
(`? condition : goto "label"`), `exit`, or waits (`~5`) are parsed line by line.
Keep command expressions on one line in those scripts.

This is especially important for nested array arguments used by `jsonObject`,
`jsonSelect`, `cacheSet`, and `eventOn` inline code blocks. A multi-line
expression can be parsed as an incomplete line and produce `Any`/type errors.
