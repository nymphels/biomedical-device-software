-- ============================================================
--  Hospital PACS (Imaging) Database - Practice Project
--  Author : [Your Name]
--  Purpose: Learning basic SQL with a real-world inspired theme
-- ============================================================


-- ============================================================
--  TABLE 1: Patients
--  Stores basic information about each patient.
-- ============================================================

CREATE TABLE Patients (
    patient_id   INT PRIMARY KEY,
    first_name   VARCHAR(50),
    last_name    VARCHAR(50),
    date_of_birth DATE,
    gender       VARCHAR(10)
);

INSERT INTO Patients (patient_id, first_name, last_name, date_of_birth, gender) VALUES
(1, 'Alice',   'Morgan',   '1985-03-12', 'Female'),
(2, 'James',   'Carter',   '1972-07-24', 'Male'),
(3, 'Fatima',  'Hassan',   '1990-11-05', 'Female'),
(4, 'David',   'Okonkwo',  '1968-01-30', 'Male'),
(5, 'Sofia',   'Reyes',    '2001-09-18', 'Female');


-- ============================================================
--  TABLE 2: Devices
--  Stores the imaging machines available in the hospital.
-- ============================================================

CREATE TABLE Devices (
    device_id    INT PRIMARY KEY,
    device_name  VARCHAR(100),
    device_type  VARCHAR(50),
    location     VARCHAR(100)
);

INSERT INTO Devices (device_id, device_name, device_type, location) VALUES
(1, 'MAGNETOM Vida',       'MRI',        'Radiology Wing A'),
(2, 'SOMATOM Drive',       'CT',         'Radiology Wing B'),
(3, 'Ysio Max',            'X-Ray',      'Emergency Room'),
(4, 'MAGNETOM Lumina',     'MRI',        'Neurology Department'),
(5, 'Revolution Apex',     'CT',         'Radiology Wing A');


-- ============================================================
--  TABLE 3: Images
--  Records each imaging scan taken for a patient,
--  linking to both the patient and the device used.
-- ============================================================

CREATE TABLE Images (
    image_id     INT PRIMARY KEY,
    patient_id   INT,
    device_id    INT,
    scan_date    DATE,
    body_part    VARCHAR(50),
    notes        VARCHAR(200)
);

INSERT INTO Images (image_id, patient_id, device_id, scan_date, body_part, notes) VALUES
(1, 1, 1, '2024-02-10', 'Brain',        'Routine follow-up scan'),
(2, 2, 2, '2024-03-15', 'Chest',        'Checking for pulmonary nodules'),
(3, 3, 4, '2024-04-22', 'Spine',        'Lower back pain assessment'),
(4, 4, 3, '2024-05-08', 'Left Knee',    'Post-surgery X-Ray check'),
(5, 5, 5, '2024-06-01', 'Abdomen',      'Abdominal pain investigation');


-- ============================================================
--  QUERY 1 — Simple SELECT
--  Goal: See all patients in the database.
-- ============================================================

-- This just pulls every row and column from the Patients table.
-- It is the most basic query you can write!
SELECT *
FROM Patients;


-- ============================================================
--  QUERY 2 — INNER JOIN
--  Goal: See each patient's name alongside their scan details.
--  We join Patients and Images using the shared patient_id column.
-- ============================================================

-- INNER JOIN only returns rows where there is a match in BOTH tables.
-- So patients who have no scans will NOT appear in this result.
SELECT
    p.first_name,
    p.last_name,
    i.scan_date,
    i.body_part,
    i.notes
FROM Patients p
INNER JOIN Images i ON p.patient_id = i.patient_id;


-- ============================================================
--  QUERY 3 — WHERE Filter
--  Goal: Show only the scans that were taken on an MRI machine.
--  We join all three tables, then filter by device type.
-- ============================================================

-- The WHERE clause acts like a filter: the database only gives us
-- back rows where device_type equals 'MRI'.
SELECT
    p.first_name,
    p.last_name,
    d.device_name,
    d.device_type,
    i.scan_date,
    i.body_part
FROM Images i
INNER JOIN Patients p ON i.patient_id = p.patient_id
INNER JOIN Devices  d ON i.device_id  = d.device_id
WHERE d.device_type = 'MRI';
