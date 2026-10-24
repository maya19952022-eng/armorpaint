#!/usr/bin/env python3
# Recounts docs/parity_matrix.md and rewrites its scoreboard
# Usage: python3 scripts/parity_matrix.py [--check]
#   --check  only verify that the rows are valid and the scoreboard is current, exit 1 otherwise

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MATRIX = os.path.join(ROOT, "docs", "parity_matrix.md")
STATUSES = ["✅", "🟡", "❌", "⛔"]
LABELS = {"✅": "✅ feito", "🟡": "🟡 parcial", "❌": "❌ falta", "⛔": "⛔ fora de escopo"}
BEGIN = "<!-- placar:inicio -->"
END = "<!-- placar:fim -->"
TARGET = 0.9


def parse(text):
	areas = []
	errors = []
	seen = set()
	area = None
	for n, line in enumerate(text.split("\n"), 1):
		m = re.match(r"^## (4\.\d+) (.+)$", line)
		if m:
			area = {"name": m.group(2).strip(), "rows": []}
			areas.append(area)
			continue
		if line.startswith("## "):
			area = None
			continue
		if area is None or not re.match(r"^\| [A-Z]+-\d+ \|", line):
			continue
		cells = [c.strip() for c in line.strip().strip("|").split("|")]
		if len(cells) != 8:
			errors.append(f"line {n}: expected 8 columns, found {len(cells)}")
			continue
		rid, status = cells[0], cells[2]
		if rid in seen:
			errors.append(f"line {n}: duplicated id {rid}")
		seen.add(rid)
		if status not in LABELS.values():
			errors.append(f"line {n}: {rid} has status '{status}', use one of {', '.join(LABELS.values())}")
			continue
		area["rows"].append((rid, status.split()[0]))
	return areas, errors


def scoreboard(areas):
	lines = [BEGIN, "", "| Área | Itens em escopo | ✅ | 🟡 | ❌ | % ✅ |", "|---|---|---|---|---|---|"]
	total = {s: 0 for s in STATUSES}
	for a in areas:
		count = {s: 0 for s in STATUSES}
		for _, s in a["rows"]:
			count[s] += 1
			total[s] += 1
		scope = count["✅"] + count["🟡"] + count["❌"]
		pct = round(100 * count["✅"] / scope) if scope else 0
		lines.append(f"| {a['name']} | {scope} | {count['✅']} | {count['🟡']} | {count['❌']} | {pct}% |")
	scope = total["✅"] + total["🟡"] + total["❌"]
	pct = round(100 * total["✅"] / scope) if scope else 0
	goal = -(-int(scope * TARGET * 100) // 100)  # ceil without float noise
	lines.append(f"| **Total** | **{scope}** | **{total['✅']} ({pct}%)** | **{total['🟡']}** | **{total['❌']}** | meta: {goal} ✅ |")
	lines.append("")
	lines.append(f"Fora de escopo nas tabelas: {total['⛔']}. Faltam {max(0, goal - total['✅'])} itens ✅ para a meta.")
	lines.append("")
	lines.append(END)
	return "\n".join(lines), scope


def main():
	check = "--check" in sys.argv[1:]
	with open(MATRIX, encoding="utf-8") as f:
		text = f.read()
	areas, errors = parse(text)
	if BEGIN not in text or END not in text:
		errors.append(f"scoreboard markers {BEGIN} {END} not found")
	board, scope = scoreboard(areas)
	if scope != 129:
		print(f"note: {scope} items in scope, the plan counts 129")
	for e in errors:
		print(f"error: {e}")
	if errors:
		return 1
	start = text.index(BEGIN)
	end = text.index(END) + len(END)
	updated = text[:start] + board + text[end:]
	if check:
		if updated != text:
			print("scoreboard is out of date, run: python3 scripts/parity_matrix.py")
			return 1
		print("parity matrix ok")
		return 0
	if updated != text:
		with open(MATRIX, "w", encoding="utf-8", newline="\n") as f:
			f.write(updated)
		print("scoreboard updated")
	else:
		print("scoreboard already current")
	return 0


if __name__ == "__main__":
	sys.exit(main())
