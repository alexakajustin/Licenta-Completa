
import zipfile
import os
import shutil
import xml.etree.ElementTree as ET

def extract_docx(docx_path, output_dir):
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    # Extract images
    with zipfile.ZipFile(docx_path, 'r') as zip_ref:
        for file in zip_ref.namelist():
            if file.startswith('word/media/'):
                zip_ref.extract(file, output_dir)
                # Move to a cleaner directory
                src = os.path.join(output_dir, file)
                dst = os.path.join(output_dir, os.path.basename(file))
                shutil.move(src, dst)
        
        # Clean up empty word/media folders
        if os.path.exists(os.path.join(output_dir, 'word')):
            shutil.rmtree(os.path.join(output_dir, 'word'))

        # Extract text content
        with zip_ref.open('word/document.xml') as f:
            xml_content = f.read()
            tree = ET.fromstring(xml_content)
            
            # Simplified text extraction (namespacing handled by tag names)
            text_nodes = tree.findall('.//{http://schemas.openxmlformats.org/wordprocessingml/2006/main}t')
            text = "".join([t.text for t in text_nodes if t.text])
            
            with open(os.path.join(output_dir, 'content.txt'), 'w', encoding='utf-8') as text_file:
                text_file.write(text)

if __name__ == "__main__":
    docx_path = r"c:\Users\Jastin\Desktop\Licenta-Completa\Licenta.docx"
    output_dir = r"c:\Users\Jastin\Desktop\Licenta-Completa\extracted_content"
    extract_docx(docx_path, output_dir)
    print(f"Extraction complete. Check {output_dir}")
