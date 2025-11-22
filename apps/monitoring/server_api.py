"""
FastAPI server exposing /api/add_sensor_data endpoint.

Implements the database write path described in the architecture and
flowchart #5 (JSON ingestion with validation and persistence).
"""

from typing import Any, Dict, Optional

import asyncpg
from fastapi import Depends, FastAPI, HTTPException, status
from pydantic import BaseModel, Field


class SensorData(BaseModel):
    device_id: str = Field(..., example="esp32-s3-01")
    turbidity_idx: float
    tds_ms_cm: float
    vacuum_kpa: float
    milk_temp_c: float
    timestamp: float
    metadata: Dict[str, Any] | None = Field(default_factory=dict)


app = FastAPI(title="Milkwarden Sensor API")
pool: Optional[asyncpg.pool.Pool] = None


async def get_pool() -> asyncpg.pool.Pool:
    if pool is None:
        raise HTTPException(status_code=500, detail="DB pool not initialized")
    return pool


@app.on_event("startup")
async def startup() -> None:
    global pool
    pool = await asyncpg.create_pool(
        user="milkwarden",
        password="milkwarden",
        host="localhost",
        port=5432,
        database="milkwarden",
        min_size=1,
        max_size=5,
    )


@app.on_event("shutdown")
async def shutdown() -> None:
    global pool
    if pool:
        await pool.close()
        pool = None


@app.post("/api/add_sensor_data", status_code=status.HTTP_201_CREATED)
async def add_sensor_data(
    payload: SensorData, db: asyncpg.pool.Pool = Depends(get_pool)
) -> Dict[str, Any]:
    query = """
        INSERT INTO sensor_data (
            device_id, turbidity_idx, tds_ms_cm,
            vacuum_kpa, milk_temp_c, ts, metadata
        )
        VALUES ($1, $2, $3, $4, $5, to_timestamp($6), $7::jsonb)
        RETURNING id;
    """
    async with db.acquire() as conn:
        record = await conn.fetchrow(
            query,
            payload.device_id,
            payload.turbidity_idx,
            payload.tds_ms_cm,
            payload.vacuum_kpa,
            payload.milk_temp_c,
            payload.timestamp,
            payload.metadata or {},
        )
    return {"status": "ok", "id": record["id"]}
