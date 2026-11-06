// TFEX-DIFF S-native: TF.EX's Changelog.Parse reads CHANGELOG.md with Markdig (Markdown.Parse -> heading / list / paragraph blocks); the
// Switch has no Markdig. The same blocks for the markdown TF.EX's changelog is written in (ATX headings `## ` / `### `, `-` / `*` / `+`
// list items, nested by indent, continuation lines joined by a space as Markdig's soft line break; inline text taken as written: the
// file has no emphasis, code or links), then TF.EX's own steps on them. The private helpers below are TF.EX's (Changelog.cs) unchanged
// except ToDisplayText: no Unicode normalization on the Switch, so accented letters fold to a space instead of their base letter (the
// file is ASCII apart from the dashes and quotes TF.EX replaces first).
using System;
using System.Collections.Generic;
using TF.EX.Domain.Utils;

namespace TfexGlue
{
    public static class ChangelogMarkdown
    {
        public static List<ChangelogRelease> Parse(string markdown)
        {
            var releases = new List<ChangelogRelease>();
            ChangelogRelease release = null;
            ChangelogSection section = null;
            string item = null;

            foreach (var raw in (markdown ?? "").Split('\n'))   // (TrimEnd below drops a \r)
            {
                var line = raw.TrimEnd();
                var trimmed = line.TrimStart();
                bool bullet = trimmed.StartsWith("- ") || trimmed.StartsWith("* ") || trimmed.StartsWith("+ ");

                if (item != null && (trimmed.Length == 0 || bullet || trimmed.StartsWith("#")))
                {
                    if (section is not null) section.Entries.Add(ToEntry(ToDisplayText(item)));
                    item = null;
                }

                if (line.StartsWith("## "))
                {
                    section = null;
                    release = ToRelease(line.Substring(3).Trim());
                    if (release is not null) releases.Add(release);
                }
                else if (line.StartsWith("### "))
                {
                    if (release is not null)
                    {
                        section = new ChangelogSection(ToDisplayText(line.Substring(4).Trim()), new List<ChangelogEntry>());
                        release.Sections.Add(section);
                    }
                }
                else if (line.StartsWith("#"))
                {
                    section = null;   // another heading level ends the list (Markdig: a new block)
                }
                else if (bullet)
                {
                    item = trimmed.Substring(2).Trim();
                }
                else if (item != null && trimmed.Length > 0)
                {
                    item = item + " " + trimmed;   // a lazy continuation line (Markdig's soft line break: " ")
                }
            }

            if (item != null && section is not null) section.Entries.Add(ToEntry(ToDisplayText(item)));

            foreach (var parsed in releases)
            {
                parsed.Sections.RemoveAll(empty => empty.Entries.Count == 0);
            }

            releases.RemoveAll(empty => empty.Sections.Count == 0);

            return releases;
        }

        private static ChangelogRelease ToRelease(string heading)
        {
            var parts = heading.Split(" - ", 2);

            return TryParseVersion(parts[0].Trim(' ', '[', ']'), out var version)
                ? new ChangelogRelease(version, parts.Length > 1 ? parts[1].Trim() : "", new List<ChangelogSection>())
                : null;
        }

        private static ChangelogEntry ToEntry(string text)
        {
            var tagEnd = text.IndexOf(']');

            return text.Length > 0 && text[0] == '[' && tagEnd > 1
                ? new ChangelogEntry(text.Substring(1, tagEnd - 1).Trim(), ToBracketFree(text.Substring(tagEnd + 1).Trim()))
                : new ChangelogEntry("", ToBracketFree(text));
        }

        private static string ToBracketFree(string text) => text.Replace('[', '(').Replace(']', ')');

        private static bool TryParseVersion(string text, out Version version)
        {
            var core = (text ?? "").Trim().TrimStart('v', 'V').Split('-', '+')[0];

            return Version.TryParse(core, out version);
        }

        private static string ToDisplayText(string text)
        {
            var folded = text
                .Replace('\u2014', '-')
                .Replace('\u2013', '-')
                .Replace('\u2019', '\'')
                .Replace('\u201C', '"')
                .Replace('\u201D', '"');

            var ascii = new char[folded.Length];

            for (int i = 0; i < folded.Length; i++)
            {
                var character = folded[i];
                ascii[i] = character >= ' ' && character <= '~' ? character : ' ';
            }

            return string.Join(" ", new string(ascii).Split(' ', StringSplitOptions.RemoveEmptyEntries)).ToUpperInvariant();
        }
    }
}
