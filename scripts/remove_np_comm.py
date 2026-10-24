import sys
import zipfile
import struct
import io

def remove_np_comm_id(sfo_data):
    header = struct.unpack('<IIII', sfo_data[:16])
    magic, version, key_offset, data_offset, num_entries = struct.unpack('<IIIII', sfo_data[:20])
    
    entries = []
    keys = []
    values = []
    
    for i in range(num_entries):
        k_ofs, fmt, length, max_len, d_ofs = struct.unpack('<HHLII', sfo_data[20+i*16:36+i*16])
        key = sfo_data[key_offset+k_ofs:].split(b'\0')[0].decode('utf-8')
        val = sfo_data[data_offset+d_ofs:data_offset+d_ofs+max_len]
        
        if key == 'NP_COMMUNICATION_ID':
            entries.append((fmt, 1, max_len)) # Set length to 1 for just the null terminator
            keys.append(key.encode('utf-8') + b'\0')
            values.append(b'\x00' + b'\0' * (max_len - 1)) # Pad the rest of max_len with zeros
        else:
            entries.append((fmt, length, max_len))
            keys.append(key.encode('utf-8') + b'\0')
            values.append(val)
            
    # rebuild SFO
    new_num_entries = len(entries)
    
    keys_block = b''.join(keys)
    # align keys block to 4 bytes
    while len(keys_block) % 4 != 0:
        keys_block += b'\0'
        
    new_key_offset = 20 + new_num_entries * 16
    new_data_offset = new_key_offset + len(keys_block)
    
    out = io.BytesIO()
    out.write(struct.pack('<IIIII', magic, version, new_key_offset, new_data_offset, new_num_entries))
    
    k_ofs = 0
    d_ofs = 0
    for i in range(new_num_entries):
        fmt, length, max_len = entries[i]
        out.write(struct.pack('<HHLII', k_ofs, fmt, length, max_len, d_ofs))
        k_ofs += len(keys[i])
        d_ofs += max_len
        
    out.write(keys_block)
    for v in values:
        out.write(v)
        
    return out.getvalue()

def process_vpk(vpk_path):
    # Read the VPK, extract SFO, remove NP_COMMUNICATION_ID, replace SFO
    with zipfile.ZipFile(vpk_path, 'r') as z:
        sfo_data = z.read('sce_sys/param.sfo')
        
    if b'NP_COMMUNICATION_ID' not in sfo_data:
        print("NP_COMMUNICATION_ID not found in SFO.")
        return
        
    new_sfo_data = remove_np_comm_id(sfo_data)
    
    # We must rebuild the zip since we can't easily replace a file in Python's zipfile
    import os, tempfile, shutil
    temp_dir = tempfile.mkdtemp()
    try:
        with zipfile.ZipFile(vpk_path, 'r') as z:
            z.extractall(temp_dir)
            
        with open(os.path.join(temp_dir, 'sce_sys', 'param.sfo'), 'wb') as f:
            f.write(new_sfo_data)
            
        with zipfile.ZipFile(vpk_path, 'w', zipfile.ZIP_DEFLATED) as z:
            for root, dirs, files in os.walk(temp_dir):
                for file in files:
                    file_path = os.path.join(root, file)
                    arcname = os.path.relpath(file_path, temp_dir)
                    z.write(file_path, arcname)
    finally:
        shutil.rmtree(temp_dir)

if __name__ == '__main__':
    process_vpk(sys.argv[1])
