import json
import os
import psycopg2
from psycopg2.extras import execute_values
import re

DB_NAME = "cellmap"
DB_USER = "postgres"
DB_PASSWORD = "postgres"
DB_HOST = "localhost"
DB_PORT = 5434

DATA_DIR = "../data"

def parse_cell_info(cell_info_str):
    cells = []
    
    pattern = r'CellIdentity(\w+):\{([^}]+)\}:CellSignalStrength\1:\s*(.+?)(?=;CellIdentity|$)'
    matches = re.findall(pattern, cell_info_str)
    
    for match in matches:
        cell_type = match[0]
        identity = match[1]
        signal = match[2]
        
        cell_data = {}
        
        if cell_type == "Gsm":
            lac_match = re.search(r'mLac=(\d+)', identity)
            cid_match = re.search(r'mCid=(\d+)', identity)
            arfcn_match = re.search(r'mArfcn=(\d+)', identity)
            mcc_match = re.search(r'mMcc=(\d+)', identity)
            mnc_match = re.search(r'mMnc=(\d+)', identity)
            
            rssi_match = re.search(r'rssi=(-?\d+)', signal)
            
            if lac_match:
                cell_data['tac'] = int(lac_match.group(1))
            if cid_match:
                cell_data['ci'] = int(cid_match.group(1))
            if arfcn_match:
                cell_data['earfcn'] = int(arfcn_match.group(1))
            if mcc_match:
                cell_data['mcc'] = int(mcc_match.group(1))
            if mnc_match:
                cell_data['mnc'] = int(mnc_match.group(1))
            if rssi_match:
                cell_data['dbm'] = int(rssi_match.group(1))
            
            cell_data['type'] = "GSM"
            cell_data['pci'] = 0
            
        elif cell_type == "Lte":
            pci_match = re.search(r'mPci=(\d+)', identity)
            tac_match = re.search(r'mTac=(\d+)', identity)
            ci_match = re.search(r'mCi=(\d+)', identity)
            earfcn_match = re.search(r'mEarfcn=(\d+)', identity)
            mcc_match = re.search(r'mMcc=(\d+)', identity)
            mnc_match = re.search(r'mMnc=(\d+)', identity)
            
            rsrp_match = re.search(r'rsrp=(-?\d+)', signal)
            rsrq_match = re.search(r'rsrq=(-?\d+)', signal)
            rssnr_match = re.search(r'rssnr=(-?\d+)', signal)
            
            if pci_match:
                cell_data['pci'] = int(pci_match.group(1))
            if tac_match:
                cell_data['tac'] = int(tac_match.group(1))
            if ci_match:
                cell_data['ci'] = int(ci_match.group(1))
            if earfcn_match:
                cell_data['earfcn'] = int(earfcn_match.group(1))
            if mcc_match:
                cell_data['mcc'] = int(mcc_match.group(1))
            if mnc_match:
                cell_data['mnc'] = int(mnc_match.group(1))
            if rsrp_match:
                cell_data['rsrp'] = int(rsrp_match.group(1))
            if rsrq_match:
                cell_data['rsrq'] = int(rsrq_match.group(1))
            if rssnr_match:
                cell_data['rssnr'] = int(rssnr_match.group(1))
            
            cell_data['type'] = "LTE"
            cell_data['dbm'] = cell_data.get('rsrp', -120)
        
        elif cell_type == "Wcdma":
            psc_match = re.search(r'mPsc=(\d+)', identity)
            uarfcn_match = re.search(r'mUarfcn=(\d+)', identity)
            mcc_match = re.search(r'mMcc=(\d+)', identity)
            mnc_match = re.search(r'mMnc=(\d+)', identity)
            
            dbm_match = re.search(r'dbm=(-?\d+)', signal)
            
            if psc_match:
                cell_data['pci'] = int(psc_match.group(1))
            if uarfcn_match:
                cell_data['earfcn'] = int(uarfcn_match.group(1))
            if mcc_match:
                cell_data['mcc'] = int(mcc_match.group(1))
            if mnc_match:
                cell_data['mnc'] = int(mnc_match.group(1))
            if dbm_match:
                cell_data['dbm'] = int(dbm_match.group(1))
            
            cell_data['type'] = "WCDMA"
            cell_data['rsrp'] = cell_data.get('dbm', -120)
        
        if cell_data:
            cells.append(cell_data)
    
    return cells

conn = psycopg2.connect(
    dbname=DB_NAME,
    user=DB_USER,
    password=DB_PASSWORD,
    host=DB_HOST,
    port=DB_PORT
)
cur = conn.cursor()

# Создаём таблицы в соответствии с init.sql
cur.execute("""
CREATE TABLE IF NOT EXISTS measurements (
    id SERIAL PRIMARY KEY,
    timestamp BIGINT,
    imei TEXT
);
""")

cur.execute("""
CREATE TABLE IF NOT EXISTS locations (
    id SERIAL PRIMARY KEY,
    measurement_id INT REFERENCES measurements(id) ON DELETE CASCADE,
    latitude DOUBLE PRECISION,
    longitude DOUBLE PRECISION,
    altitude DOUBLE PRECISION,
    accuracy DOUBLE PRECISION,
    speed DOUBLE PRECISION
);
""")

cur.execute("""
CREATE TABLE IF NOT EXISTS cells (
    id SERIAL PRIMARY KEY,
    measurement_id INT REFERENCES measurements(id) ON DELETE CASCADE,
    type TEXT,
    dbm INT,
    rsrp INT,
    pci INT,
    tac INT,
    mcc INT,
    mnc INT,
    ci BIGINT,
    earfcn INT
);
""")

cur.execute("""
CREATE TABLE IF NOT EXISTS traffic (
    id SERIAL PRIMARY KEY,
    measurement_id INT REFERENCES measurements(id) ON DELETE CASCADE,
    mobile_rx BIGINT,
    mobile_tx BIGINT,
    total_rx BIGINT,
    total_tx BIGINT
);
""")

# Индексы
cur.execute("CREATE INDEX IF NOT EXISTS idx_locations_coords ON locations(latitude, longitude);")
cur.execute("CREATE INDEX IF NOT EXISTS idx_cells_signal ON cells(dbm);")
cur.execute("CREATE INDEX IF NOT EXISTS idx_measurements_timestamp ON measurements(timestamp);")

conn.commit()

# Получаем все JSON файлы из папки
json_files = [f for f in os.listdir(DATA_DIR) if f.endswith(".json")]

for file in json_files:
    path = os.path.join(DATA_DIR, file)
    print(f"Обрабатываем {path} ...")
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
        
        if isinstance(data, dict):
            data = [data]
        
        for item in data:
            timestamp = item.get("timestamp")
            imei = item.get("imei", "")
            
            cur.execute("""
                INSERT INTO measurements (timestamp, imei)
                VALUES (%s, %s) RETURNING id
            """, (timestamp, imei))
            measurement_id = cur.fetchone()[0]
            
            # Location
            loc = {}
            if "location" in item and item["location"]:
                loc = item["location"]
            else:
                loc = {
                    "latitude": item.get("latitude"),
                    "longitude": item.get("longitude"),
                    "altitude": item.get("altitude"),
                    "accuracy": item.get("accuracy"),
                    "speed": item.get("speed")
                }
            
            if loc and loc.get("latitude") is not None:
                cur.execute("""
                    INSERT INTO locations (measurement_id, latitude, longitude, altitude, accuracy, speed)
                    VALUES (%s, %s, %s, %s, %s, %s)
                """, (
                    measurement_id,
                    loc.get("latitude"),
                    loc.get("longitude"),
                    loc.get("altitude"),
                    loc.get("accuracy"),
                    loc.get("speed")
                ))
            
            # Cells
            cell_rows = []
            
            if "telephony" in item and item["telephony"]:
                for c_name, c in item["telephony"].items():
                    if isinstance(c, dict):
                        cell_rows.append((
                            measurement_id,
                            c.get("type"),
                            c.get("dbm"),
                            c.get("rsrp"),
                            c.get("pci"),
                            c.get("tac") or c.get("lac"),
                            c.get("mcc"),
                            c.get("mnc"),
                            c.get("ci") or c.get("cid"),
                            c.get("earfcn")
                        ))
            
            if "cellInfo" in item and item["cellInfo"]:
                cells = parse_cell_info(item["cellInfo"])
                for cell in cells:
                    cell_rows.append((
                        measurement_id,
                        cell.get("type"),
                        cell.get("dbm"),
                        cell.get("rsrp"),
                        cell.get("pci"),
                        cell.get("tac"),
                        cell.get("mcc"),
                        cell.get("mnc"),
                        cell.get("ci"),
                        cell.get("earfcn")
                    ))
            
            if cell_rows:
                execute_values(
                    cur,
                    """INSERT INTO cells 
                    (measurement_id, type, dbm, rsrp, pci, tac, mcc, mnc, ci, earfcn)
                    VALUES %s""",
                    cell_rows
                )
            
            # Traffic
            traffic_data = item.get("traffic", {})
            if traffic_data:
                cur.execute("""
                    INSERT INTO traffic (measurement_id, mobile_rx, mobile_tx, total_rx, total_tx)
                    VALUES (%s, %s, %s, %s, %s)
                """, (
                    measurement_id,
                    traffic_data.get("mobile_rx_bytes"),
                    traffic_data.get("mobile_tx_bytes"),
                    traffic_data.get("total_rx_bytes"),
                    traffic_data.get("total_tx_bytes")
                ))
        
        conn.commit()
        print(f"Вставлены записи из {file}")

cur.close()
conn.close()
print("Импорт завершён.")