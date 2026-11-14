# GBMDevice — Breath-Based Acetone Sensor for Exploratory Glioblastoma Monitoring

A portable, low-cost, non-invasive breath-sensing system designed to investigate whether volatile organic compound (VOC) biomarkers—specifically acetone—can be reliably captured for early exploratory detection of glioblastoma (GBM) recurrence.

---

## Overview

Glioblastoma multiforme (GBM) is among the most aggressive brain malignancies, with recurrence presenting severe clinical monitoring challenges. Current standards (MRI, tissue biopsy) are capital-intensive, intermittent, and invasive.

Elevated breath acetone is an explored metabolic biomarker associated with altered cellular energetics. This project investigates the feasibility of detecting targeted VOC concentrations using an accessible, microcontroller-interfaced gas-sensing array in controlled synthetic laboratory conditions.

> **Research Stage:** Exploratory laboratory prototype utilizing synthetic test chambers. Non-clinical / non-human diagnostic.

---

## System Architecture

```text
[ Breath / Synthetic Chamber ]
            │
            ▼
[ Gas Sensing Array (VOC / Acetone) ]
            │ (Analog / ADC Signal)
            ▼
[ Microcontroller Unit (Raspberry Pi / Arduino) ]
            │ (Serial / SPI Stream)
            ▼
[ Embedded C++ Acquisition & Calibration Engine ]
            │ (Real-time Filtering & Baseline Tracking)
            ▼
[ Telemetry Output / Data Logging ]
```

---

## Hardware Specifications

* **Processing Core:** Raspberry Pi / Microcontroller platform
* **Sensor Modality:** Metal-oxide semiconductor (MOS) / VOC gas sensor array
* **Measurement Target:** Trace acetone concentration (C3H6O)
* **Data Ingestion:** High-resolution ADC conversion with dynamic baseline compensation

---

## Software & Signal Processing

The firmware is written in **C++** and implements:
1. **Sensor Pre-heating & Baseline Stabilization:** Automated calibration routine to account for ambient temperature and relative humidity drift.
2. **Moving-Average Filtering:** Noise reduction over continuous analog sampling windows.
3. **Delta Response Calculation:** Computes ratio of baseline resistance (R0) to active resistance (Rs) to infer relative VOC concentration.

---

## Recognition & Science Presentation

Nominated for the **Truist Pride Award** for student innovation at the intersection of medical engineering and embedded computing.
