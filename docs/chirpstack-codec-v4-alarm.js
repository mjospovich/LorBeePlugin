function decodeUplink(input) {
  var b = input.bytes;

  // --- Legacy 4-byte (format: legacy) ---
  if (b.length === 4) {
    var tRaw = (b[0] << 8) | b[1];
    var t = (tRaw & 0x8000) ? tRaw - 0x10000 : tRaw;
    var h = (b[2] << 8) | b[3];
    if (t === 0x7fff && h === 0xffff) return { data: { valid: false } };
    return { data: { temperature_c: t / 100, humidity_pct: h / 100, valid: true } };
  }

  if (b.length < 2) return { data: {} };
  var o = 0;
  var ver = b[o++];
  var flags = b[o++];
  var includeStatus = (flags & 1) !== 0;
  var hasAck = (flags & 2) !== 0;

  // --- Sensor Type Registry (same on every edge) ---
  var TYPES = {
    0x01: { name: 'climate', fields: ['temperature', 'humidity'] },
    0x02: { name: 'motion',  fields: ['occupancy', 'illumination'] },
    0x03: { name: 'contact', fields: ['contact'] },
    0x04: { name: 'air_quality', fields: ['pm1_0', 'pm2_5', 'pm4_0', 'pm10'] },
    0x05: { name: 'air_quality_aqi', fields: ['pm1_0', 'pm2_5', 'pm4_0', 'pm10', 'aqi'] },
    0x06: { name: 'environment', fields: ['temperature', 'humidity', 'pressure_hpa', 'gas_resistance_ohm'] }
  };

  // Optional: human-friendly labels for entry IDs. Customize per deployment.
  var LABELS = {};

  function readI16() {
    var raw = (b[o] << 8) | b[o + 1]; o += 2;
    return (raw & 0x8000) ? raw - 0x10000 : raw;
  }
  function readU16() { var v = (b[o] << 8) | b[o + 1]; o += 2; return v; }
  function readU32() {
    var v = ((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]) >>> 0;
    o += 4;
    return v;
  }
  function readU8() { return b[o++]; }

  function readField(fname) {
    if (fname === 'temperature') {
      var raw = readI16();
      return raw === 0x7fff ? null : raw / 100;
    }
    if (fname === 'humidity') {
      var raw = readU16();
      return raw === 0xffff ? null : raw / 100;
    }
    if (fname === 'occupancy' || fname === 'motion') {
      var v = readU8();
      return v === 0xff ? null : v !== 0;
    }
    if (fname === 'illumination' || fname === 'brightness') {
      var v = readU8();
      var names = ['unknown', 'dark', 'medium', 'bright'];
      return v < names.length ? names[v] : 'unknown';
    }
    if (fname === 'contact') {
      var v = readU8();
      return v === 0xff ? null : v !== 0;
    }
    if (fname === 'pm1_0' || fname === 'pm2_5' || fname === 'pm4_0' || fname === 'pm10') {
      var raw = readU16();
      return raw === 0xffff ? null : raw / 10;
    }
    if (fname === 'aqi') {
      var aq = readU16();
      return aq === 0xffff ? null : aq;
    }
    if (fname === 'pressure_hpa') {
      var ph = readU16();
      return ph === 0xffff ? null : ph;
    }
    if (fname === 'gas_resistance_ohm') {
      var gr = readU32();
      return gr === 0xffffffff ? null : gr;
    }
    return null;
  }

  function readStatus(row) {
    row.linkquality = readU8();
    var bat = readU8();
    row.battery_pct = bat === 0xff ? null : bat;
    var vRaw = readU16();
    row.voltage_mV = vRaw === 0xffff ? null : vRaw;
  }

  var FIELD_KEYS = {
    temperature: 'temperature_c',
    humidity: 'humidity_pct',
    occupancy: 'occupancy',
    motion: 'occupancy',
    illumination: 'illumination',
    brightness: 'illumination',
    contact: 'contact',
    pm1_0: 'pm1_0_ugm3',
    pm2_5: 'pm2_5_ugm3',
    pm4_0: 'pm4_0_ugm3',
    pm10: 'pm10_ugm3',
    aqi: 'aqi',
    pressure_hpa: 'pressure_hpa',
    gas_resistance_ohm: 'gas_resistance_ohm'
  };

  var out = {};
  function alarmFieldNames(typeDef, alarmMask) {
    var names = [];
    for (var i = 0; i < typeDef.fields.length && i < 8; i++) {
      if ((alarmMask & (1 << i)) !== 0) {
        var fn = typeDef.fields[i];
        names.push(FIELD_KEYS[fn] || fn);
      }
    }
    return names;
  }

  // --- v4/v5: self-describing entries ---
  // v4: sensor_type bit7 = alarm flag, bits0..6 = type ID.
  // v5: sensor_type uses bits0..6 only, and every entry carries one alarm mask byte.
  if (ver === 0x04 || ver === 0x05) {
    if (hasAck && o + 2 <= b.length) {
      var ackCmd = readU8();
      var ackOk = readU8();
      out._ack = { cmd_id: ackCmd, success: ackOk !== 0 };
    }
    while (o < b.length) {
      var eid = readU8();
      var stypeRaw = readU8();
      var stype = stypeRaw & 0x7f;

      var td = TYPES[stype];
      if (!td) {
        out['unknown_type_' + stype] = { entry_id: eid, error: 'unknown_sensor_type' };
        break;
      }
      var key = LABELS[eid] !== undefined ? LABELS[eid] : td.name + '_' + eid;
      var row = { entry_id: eid, sensor_type: td.name };
      var alarmMask = 0;
      if (ver === 0x05 && o < b.length) {
        alarmMask = readU8();
      }
      for (var fi = 0; fi < td.fields.length; fi++) {
        var fn = td.fields[fi];
        row[FIELD_KEYS[fn] || fn] = readField(fn);
      }
      if (ver === 0x05) {
        var af = alarmFieldNames(td, alarmMask);
        if (af.length > 0) row.alarm_fields = af;
      }
      if (includeStatus) readStatus(row);
      out[key] = row;
    }
    return { data: out };
  }

  // --- v3: legacy positional (backward compat) ---
  if (ver === 0x03) {
    var V3_PLAN = [
      { id: 1, fields: ['temperature', 'humidity'] },
      { id: 2, fields: ['occupancy', 'illumination'] }
    ];
    var V3_LABELS = { 1: 'air', 2: 'motion' };
    for (var pi = 0; pi < V3_PLAN.length; pi++) {
      var step = V3_PLAN[pi];
      if (o >= b.length) break;
      var eid = readU8();
      var key = V3_LABELS[eid] !== undefined ? V3_LABELS[eid] : ('id_' + eid);
      var row = { entry_id: eid };
      if (eid !== step.id) row._id_mismatch = true;
      for (var fi = 0; fi < step.fields.length; fi++) {
        var fn = step.fields[fi];
        row[FIELD_KEYS[fn] || fn] = readField(fn);
      }
      if (includeStatus) readStatus(row);
      out[key] = row;
    }
    return { data: out };
  }

  return { data: { error: 'unknown_payload_version', ver: ver } };
}
