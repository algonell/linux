CODESPELL = "codespell.txt"
EXCLUDE = ["* ", "@", "MODULE_"]


def find(text, substring):
    occurrences = []
    start = 0

    while True:
        start = text.find(substring, start)

        if start == -1:
            break

        occurrences.append(start)

        start += len(substring)

    return occurrences


with open(CODESPELL, "r", encoding="UTF-8") as file:
    while line := file.readline():
        try:
            split = line.split(" ")
            loc = split[0].split(":")
            source = loc[0]

            if source.endswith(".c") or source.endswith(".h"):
                lines = open(source, "r", encoding="UTF-8").readlines()
                content = lines[int(loc[1]) - 1]
                check = find(content, '"')

                if len(check) > 1:
                    word = split[1]

                    if check[0] < content.index(word) < check[-1] and all(
                        x not in content for x in EXCLUDE
                    ):
                        print(line, content)
        except Exception as e:
            print(e, line)
