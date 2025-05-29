from clang import cindex

CLANG_LIB = "/usr/lib/llvm-18/lib/libclang-18.so"
SOURCE_FILE = "target.c"
OUTPUT_FILE = "target_modified.c"

cindex.Config.set_library_file(CLANG_LIB)

index = cindex.Index.create()
tu = index.parse(SOURCE_FILE, args=["-std=c99"])

#원본 소스 읽기
with open(SOURCE_FILE, "r") as f:
    lines = f.readlines()

#수정할 삽입 정보 리스트
insertions = []  # (line_num, insert_code)

def walk(cursor):
    if (cursor.kind == cindex.CursorKind.VAR_DECL and
        cursor.type.kind == cindex.TypeKind.CONSTANTARRAY and
        cursor.location.file and cursor.location.file.name == SOURCE_FILE):

        name = cursor.spelling
        length = cursor.type.element_count
        line_num = cursor.extent.start.line
        original_line = lines[line_num - 1]
        
        indent = original_line[:len(original_line) - len(original_line.lstrip())]
        insert_code = f"{indent}int {name}_len = {length};\n"
        insertions.append((line_num, insert_code))
        print(f"📌 배열: {name}, 길이: {length}, 삽입 위치: {line_num}")

    for child in cursor.get_children():
        walk(child)

walk(tu.cursor)

#라인에 삽입
for line_num, code in sorted(insertions, reverse=True):
    lines.insert(line_num - 1, code)

#수정된 파일 저장
with open(OUTPUT_FILE, "w") as f:
    f.writelines(lines)

print(f"✅ 수정된 C 파일 저장 완료: {OUTPUT_FILE}")
