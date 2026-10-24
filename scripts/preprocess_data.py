import os
import subprocess
import sys
from pathlib import Path

def run_preprocessor(data_win_path, output_dir=None, atlas_size=2048):
    root_dir = Path(__file__).resolve().parent.parent
    preprocessor_dir = root_dir / "src" / "ButterscotchPreprocessor-main"
    
    if not preprocessor_dir.exists():
        print(f"Error: Preprocessor folder not found at {preprocessor_dir}")
        return False
        
    data_win = Path(data_win_path).resolve()
    if not data_win.exists():
        print(f"Error: data.win file not found at {data_win}")
        return False
        
    if output_dir is None:
        output_dir = data_win.parent / "processed"
    else:
        output_dir = Path(output_dir).resolve()
        
    output_dir.mkdir(parents=True, exist_ok=True)
    
    gradlew = preprocessor_dir / ("gradlew.bat" if sys.platform == "win32" else "gradlew")
    cmd = [
        str(gradlew),
        ":processor-cli:run",
        f"--args={data_win} -o {output_dir} --atlas-size {atlas_size}"
    ]
    
    print(f"Running ButterscotchPreprocessor on {data_win}...")
    result = subprocess.run(cmd, cwd=str(preprocessor_dir), shell=True)
    return result.returncode == 0

if __name__ == "__main__":
    if len(sys.argv) > 1:
        data_path = sys.argv[1]
        out_path = sys.argv[2] if len(sys.argv) > 2 else None
        run_preprocessor(data_path, out_path)
    else:
        print("Usage: python preprocess_data.py <path/to/data.win> [output/directory]")
