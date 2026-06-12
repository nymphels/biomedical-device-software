# 🏥 Hospital PACS – SQL Practice Project

Hi! This is a personal practice project I built while learning SQL as a Biomedical Technology student.

The goal was simple: get comfortable writing real database queries using a subject I actually care about — medical imaging systems (PACS). No fancy frameworks, no complex setup. Just plain SQL that I can look back at and understand.

---

## 📖 What is this project?

PACS stands for **Picture Archiving and Communication System** — it's the type of database hospitals use to store and manage medical images like MRI and CT scans.

I used that as the theme for this practice database because it's directly related to my field of study.

---

## 🗃️ What's in the database?

Three simple tables:

| Table | What it stores |
|---|---|
| `Patients` | Basic patient info (name, date of birth, gender) |
| `Devices` | Imaging machines like MRI and CT scanners |
| `Images` | Records of scans, linking patients to the device used |

Each table has 5 sample rows of realistic (but fictional) data to practice queries on.

---

## 🔍 Queries included

1. **SELECT all patients** — the simplest query possible, just to see the data
2. **INNER JOIN** — connecting patients with their imaging records
3. **WHERE filter** — pulling only MRI scans from the images table

---

## 🛠️ How to run it

You can run this script in any SQL environment. I tested it with SQLite and MySQL, but it should work in PostgreSQL too.

1. Open your SQL tool (DB Browser for SQLite, MySQL Workbench, DBeaver, etc.)
2. Open or paste the `hospital_pacs.sql` file
3. Run the whole script — it will create the tables, insert the data, and run the queries

---

## 🎯 Why I made this

I wanted something on my GitHub that shows I understand the basics:
- How to design a simple relational database
- How to write `CREATE TABLE` and `INSERT` statements
- How to use `SELECT`, `JOIN`, and `WHERE` in queries

It is not a production system. It is not meant to be impressive or complex. It is just honest, clean practice work — and that's exactly what I was going for.

---

## 📚 What I learned

- How primary keys connect tables together
- How `INNER JOIN` works and why it's useful
- How `WHERE` filters rows based on a condition
- How to organize SQL code so it's readable to someone else

---

## 🚀 What's next

Some things I want to add as I keep learning:

- [ ] Add a `Radiologists` table and link it to scans
- [ ] Try using `GROUP BY` to count scans per patient
- [ ] Write a query to find patients with more than one scan
- [ ] Explore `LEFT JOIN` and see how it differs from `INNER JOIN`

---

*Made with curiosity and a lot of trial and error. Feel free to use or adapt this for your own learning!*
