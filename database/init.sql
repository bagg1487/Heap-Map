CREATE TABLE IF NOT EXISTS measurements (
    id SERIAL PRIMARY KEY,
    timestamp BIGINT,
    imei TEXT
);

CREATE TABLE IF NOT EXISTS locations (
    id SERIAL PRIMARY KEY,
    measurement_id INT REFERENCES measurements(id) ON DELETE CASCADE,
    latitude DOUBLE PRECISION,
    longitude DOUBLE PRECISION,
    altitude DOUBLE PRECISION,
    accuracy DOUBLE PRECISION,
    speed DOUBLE PRECISION
);

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

CREATE TABLE IF NOT EXISTS traffic (
    id SERIAL PRIMARY KEY,
    measurement_id INT REFERENCES measurements(id) ON DELETE CASCADE,
    mobile_rx BIGINT,
    mobile_tx BIGINT,
    total_rx BIGINT,
    total_tx BIGINT
);

CREATE INDEX IF NOT EXISTS idx_locations_coords ON locations(latitude, longitude);
CREATE INDEX IF NOT EXISTS idx_cells_signal ON cells(dbm);
CREATE INDEX IF NOT EXISTS idx_measurements_timestamp ON measurements(timestamp);
CREATE INDEX IF NOT EXISTS idx_measurements_imei ON measurements(imei);