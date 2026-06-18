"""
DocxCompressor - Web tool to compress .docx files by optimizing embedded images.
Converts PNG to JPEG, downscales oversized images, and repacks the document.
"""

import os
import sys
import shutil
import zipfile
import glob
import tempfile
import uuid
import threading
import time
from flask import Flask, render_template, request, send_file, jsonify

try:
    from PIL import Image
except ImportError:
    print("Pillow not installed. Run: pip install Pillow")
    sys.exit(1)

app = Flask(__name__)
app.config['MAX_CONTENT_LENGTH'] = 100 * 1024 * 1024  # 100 MB max upload

# Store compression jobs and their status
jobs = {}
UPLOAD_DIR = os.path.join(tempfile.gettempdir(), "docx_compressor")
os.makedirs(UPLOAD_DIR, exist_ok=True)


def cleanup_old_files():
    """Remove files older than 1 hour from the upload directory."""
    now = time.time()
    for item in os.listdir(UPLOAD_DIR):
        path = os.path.join(UPLOAD_DIR, item)
        try:
            if now - os.path.getmtime(path) > 3600:
                if os.path.isdir(path):
                    shutil.rmtree(path)
                else:
                    os.remove(path)
        except Exception:
            pass


def compress_docx(input_path, job_id, target_mb=5.0, jpeg_quality=60, max_dimension=1200):
    """
    Compress a .docx file by converting PNG images to optimized JPEG.
    
    Args:
        input_path: Path to the input .docx file
        job_id: Unique job identifier for status tracking
        target_mb: Target maximum file size in MB
        jpeg_quality: Initial JPEG quality (1-100)
        max_dimension: Maximum image dimension in pixels
    """
    job = jobs[job_id]
    job['status'] = 'processing'
    job['progress'] = 0
    job['message'] = 'Extragere document...'

    try:
        # Create working directory
        work_dir = os.path.join(UPLOAD_DIR, f"work_{job_id}")
        extract_dir = os.path.join(work_dir, "extracted")
        os.makedirs(work_dir, exist_ok=True)

        # Copy and extract docx as ZIP
        zip_path = os.path.join(work_dir, "temp.zip")
        shutil.copy2(input_path, zip_path)
        
        with zipfile.ZipFile(zip_path, 'r') as zf:
            zf.extractall(extract_dir)
        os.remove(zip_path)

        job['progress'] = 10
        job['message'] = 'Analizare imagini...'

        # Find media directory
        media_dir = os.path.join(extract_dir, "word", "media")
        if not os.path.exists(media_dir):
            # No images to compress
            output_path = os.path.join(UPLOAD_DIR, f"compressed_{job_id}.docx")
            shutil.copy2(input_path, output_path)
            job['status'] = 'done'
            job['progress'] = 100
            job['message'] = 'Nu s-au gasit imagini de comprimat.'
            job['output_path'] = output_path
            job['original_size'] = os.path.getsize(input_path)
            job['compressed_size'] = os.path.getsize(output_path)
            shutil.rmtree(work_dir, ignore_errors=True)
            return

        # Get list of PNG files
        png_files = sorted(glob.glob(os.path.join(media_dir, "*.png")))
        total_images = len(png_files)
        
        if total_images == 0:
            output_path = os.path.join(UPLOAD_DIR, f"compressed_{job_id}.docx")
            shutil.copy2(input_path, output_path)
            job['status'] = 'done'
            job['progress'] = 100
            job['message'] = 'Nu s-au gasit imagini PNG de comprimat.'
            job['output_path'] = output_path
            job['original_size'] = os.path.getsize(input_path)
            job['compressed_size'] = os.path.getsize(output_path)
            shutil.rmtree(work_dir, ignore_errors=True)
            return

        job['message'] = f'Comprimare {total_images} imagini...'

        # Convert PNGs to JPEG
        renames = {}
        image_details = []
        
        for i, filepath in enumerate(png_files):
            filename = os.path.basename(filepath)
            new_filename = filename.replace(".png", ".jpeg")
            new_filepath = os.path.join(media_dir, new_filename)
            
            try:
                img = Image.open(filepath)
                orig_w, orig_h = img.size
                old_size = os.path.getsize(filepath)

                # Downscale large images
                if orig_w > max_dimension or orig_h > max_dimension:
                    ratio = min(max_dimension / orig_w, max_dimension / orig_h)
                    new_w = int(orig_w * ratio)
                    new_h = int(orig_h * ratio)
                    img = img.resize((new_w, new_h), Image.LANCZOS)

                # Convert to RGB for JPEG
                if img.mode in ('RGBA', 'P', 'LA'):
                    background = Image.new('RGB', img.size, (255, 255, 255))
                    if img.mode == 'P':
                        img = img.convert('RGBA')
                    if img.mode in ('RGBA', 'LA'):
                        background.paste(img, mask=img.split()[-1])
                    img = background
                elif img.mode != 'RGB':
                    img = img.convert('RGB')

                img.save(new_filepath, 'JPEG', quality=jpeg_quality, optimize=True)
                new_size = os.path.getsize(new_filepath)

                os.remove(filepath)
                renames[filename] = new_filename
                
                image_details.append({
                    'name': filename,
                    'original_size': old_size,
                    'compressed_size': new_size,
                    'original_dims': f"{orig_w}x{orig_h}",
                })

            except Exception as e:
                image_details.append({
                    'name': filename,
                    'error': str(e)
                })

            # Update progress (10% to 70% for image conversion)
            job['progress'] = 10 + int(60 * (i + 1) / total_images)
            job['message'] = f'Comprimare imagine {i + 1}/{total_images}...'

        job['progress'] = 75
        job['message'] = 'Actualizare referinte document...'

        # Update XML references
        xml_files = []
        for root, dirs, files in os.walk(extract_dir):
            for f in files:
                if f.endswith('.xml') or f.endswith('.rels'):
                    xml_files.append(os.path.join(root, f))

        for xml_path in xml_files:
            with open(xml_path, 'r', encoding='utf-8') as f:
                content = f.read()

            modified = False
            for old_name, new_name in renames.items():
                if old_name in content:
                    content = content.replace(old_name, new_name)
                    modified = True

            if modified:
                with open(xml_path, 'w', encoding='utf-8') as f:
                    f.write(content)

        # Update [Content_Types].xml
        content_types_path = os.path.join(extract_dir, "[Content_Types].xml")
        if os.path.exists(content_types_path):
            with open(content_types_path, 'r', encoding='utf-8') as f:
                content = f.read()

            if 'Extension="jpeg"' not in content and 'Extension="jpg"' not in content:
                content = content.replace(
                    '</Types>',
                    '<Default Extension="jpeg" ContentType="image/jpeg"/></Types>'
                )

            with open(content_types_path, 'w', encoding='utf-8') as f:
                f.write(content)

        job['progress'] = 85
        job['message'] = 'Reimpachetare document...'

        # Repack docx
        output_path = os.path.join(UPLOAD_DIR, f"compressed_{job_id}.docx")
        with zipfile.ZipFile(output_path, 'w', zipfile.ZIP_DEFLATED) as zf:
            for root, dirs, files in os.walk(extract_dir):
                for file in files:
                    file_path = os.path.join(root, file)
                    arcname = os.path.relpath(file_path, extract_dir)
                    zf.write(file_path, arcname)

        compressed_size = os.path.getsize(output_path)
        original_size = os.path.getsize(input_path)

        # If still over target, try harder
        if compressed_size / (1024 * 1024) > target_mb:
            job['progress'] = 88
            job['message'] = 'Inca prea mare, comprimare suplimentara...'
            
            # Re-compress with lower quality and smaller dimensions
            for filepath in sorted(glob.glob(os.path.join(media_dir, "*.jpeg"))):
                img = Image.open(filepath)
                orig_w, orig_h = img.size
                smaller_dim = 800
                if orig_w > smaller_dim or orig_h > smaller_dim:
                    ratio = min(smaller_dim / orig_w, smaller_dim / orig_h)
                    new_w = int(orig_w * ratio)
                    new_h = int(orig_h * ratio)
                    img = img.resize((new_w, new_h), Image.LANCZOS)
                if img.mode != 'RGB':
                    img = img.convert('RGB')
                img.save(filepath, 'JPEG', quality=35, optimize=True)

            os.remove(output_path)
            with zipfile.ZipFile(output_path, 'w', zipfile.ZIP_DEFLATED) as zf:
                for root, dirs, files in os.walk(extract_dir):
                    for file in files:
                        file_path = os.path.join(root, file)
                        arcname = os.path.relpath(file_path, extract_dir)
                        zf.write(file_path, arcname)
            compressed_size = os.path.getsize(output_path)

        # Cleanup work directory
        shutil.rmtree(work_dir, ignore_errors=True)

        job['status'] = 'done'
        job['progress'] = 100
        job['output_path'] = output_path
        job['original_size'] = original_size
        job['compressed_size'] = compressed_size
        job['images_processed'] = len(renames)
        job['image_details'] = image_details
        job['message'] = 'Comprimare finalizata!'

    except Exception as e:
        job['status'] = 'error'
        job['message'] = f'Eroare: {str(e)}'
        job['progress'] = 0


@app.route('/')
def index():
    cleanup_old_files()
    return render_template('index.html')


@app.route('/upload', methods=['POST'])
def upload():
    if 'file' not in request.files:
        return jsonify({'error': 'Nu s-a selectat niciun fisier.'}), 400

    file = request.files['file']
    if file.filename == '':
        return jsonify({'error': 'Nu s-a selectat niciun fisier.'}), 400

    if not file.filename.lower().endswith('.docx'):
        return jsonify({'error': 'Doar fisiere .docx sunt acceptate.'}), 400

    # Get compression settings from form
    target_mb = float(request.form.get('target_mb', 5.0))
    jpeg_quality = int(request.form.get('jpeg_quality', 60))
    max_dimension = int(request.form.get('max_dimension', 1200))

    # Save uploaded file
    job_id = str(uuid.uuid4())[:8]
    original_filename = file.filename
    input_path = os.path.join(UPLOAD_DIR, f"input_{job_id}.docx")
    file.save(input_path)

    # Create job
    jobs[job_id] = {
        'status': 'queued',
        'progress': 0,
        'message': 'In asteptare...',
        'original_filename': original_filename,
        'input_path': input_path,
    }

    # Start compression in background thread
    thread = threading.Thread(
        target=compress_docx,
        args=(input_path, job_id, target_mb, jpeg_quality, max_dimension)
    )
    thread.daemon = True
    thread.start()

    return jsonify({'job_id': job_id})


@app.route('/status/<job_id>')
def status(job_id):
    if job_id not in jobs:
        return jsonify({'error': 'Job not found.'}), 404

    job = jobs[job_id]
    response = {
        'status': job['status'],
        'progress': job['progress'],
        'message': job['message'],
    }

    if job['status'] == 'done':
        response['original_size'] = job['original_size']
        response['compressed_size'] = job['compressed_size']
        response['images_processed'] = job.get('images_processed', 0)
        response['original_filename'] = job.get('original_filename', 'document.docx')
        reduction = (1 - job['compressed_size'] / job['original_size']) * 100 if job['original_size'] > 0 else 0
        response['reduction_percent'] = round(reduction, 1)

    return jsonify(response)


@app.route('/download/<job_id>')
def download(job_id):
    if job_id not in jobs:
        return jsonify({'error': 'Job not found.'}), 404

    job = jobs[job_id]
    if job['status'] != 'done':
        return jsonify({'error': 'Fisierul nu este inca gata.'}), 400

    original_name = job.get('original_filename', 'document.docx')
    base_name = os.path.splitext(original_name)[0]
    download_name = f"{base_name}_compressed.docx"

    return send_file(
        job['output_path'],
        as_attachment=True,
        download_name=download_name,
        mimetype='application/vnd.openxmlformats-officedocument.wordprocessingml.document'
    )


if __name__ == '__main__':
    print("\n" + "=" * 55)
    print("   DocxCompressor - Compresor de documente Word")
    print("=" * 55)
    print(f"\n   Deschide in browser: http://localhost:5000")
    print(f"\n   Apasa Ctrl+C pentru a opri serverul.\n")
    print("=" * 55 + "\n")
    
    import webbrowser
    webbrowser.open('http://localhost:5000')
    
    app.run(host='0.0.0.0', port=5000, debug=False)
