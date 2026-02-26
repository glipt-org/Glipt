def make_person(name, age):
    return {"name": name, "age": age, "score": 0}

people = []
i = 0
while i < 50000:
    people.append(make_person("user" + str(i), i % 100))
    i = i + 1

# Score each person
i = 0
while i < len(people):
    p = people[i]
    p["score"] = p["age"] * 10 + len(p["name"])
    i = i + 1

# Sum all scores
total = 0
i = 0
while i < len(people):
    total = total + people[i]["score"]
    i = i + 1

print(f"Total score: {total}")
