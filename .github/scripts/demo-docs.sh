#!/usr/bin/env bash
# A made-up folder of study documents, for screenshots of Pods: Word files (.docx), text and Markdown.
set -euo pipefail
out="${1:-/tmp/docs}"
rm -rf "$out"; mkdir -p "$out/History" "$out/Misc"

# docx <file> <heading> <text>: the smallest Word file Word itself opens
docx() {
  local file="$1" heading="$2" text="$3" tmp
  tmp="$(mktemp -d)"
  mkdir -p "$tmp/_rels" "$tmp/word" "$tmp/docProps"
  cat > "$tmp/[Content_Types].xml" <<'X'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/></Types>
X
  cat > "$tmp/_rels/.rels" <<'X'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>
X
  cat > "$tmp/word/document.xml" <<X
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body><w:p><w:pPr><w:pStyle w:val="Heading1"/></w:pPr><w:r><w:t>$heading</w:t></w:r></w:p><w:p><w:r><w:t xml:space="preserve">$text</w:t></w:r></w:p></w:body></w:document>
X
  (cd "$tmp" && zip -qX -r "$file" "[Content_Types].xml" _rels word)
  rm -rf "$tmp"
}

docx "$out/History/berlin conference notes.docx" "The Berlin Conference 1884-85" "Bismarck called the Berlin Conference in 1884 so European powers could agree rules for claiming land in Africa. King Leopold of Belgium wanted the Congo Free State. The conference set the principle of effective occupation."
docx "$out/History/Leopold and the Congo.docx" "Leopold II" "King Leopold II used the Berlin Conference to gain the Congo Free State as his personal colony. Rubber quotas and forced labour in the Congo killed millions."
docx "$out/scramble for africa essay.docx" "Scramble for Africa" "The Scramble for Africa saw European powers carve up Africa between 1881 and 1914. The Berlin Conference gave rules for effective occupation. Britain, France, Germany, Belgium and Portugal claimed colonies across Africa."
printf 'Outcomes of the Berlin Conference: free trade on the Congo and Niger rivers, effective occupation, Leopold recognised in the Congo Free State, Africa partitioned by European powers.\n' > "$out/mindmap berlin conf outcome.txt"
docx "$out/cold war origins.docx" "Origins of the Cold War" "The Cold War began after 1945 as the USA and the Soviet Union disagreed at Yalta and Potsdam. Stalin wanted a buffer zone in Eastern Europe. Truman responded with containment."
printf '# Cuban Missile Crisis 1962\n\nKhrushchev placed nuclear missiles in Cuba. Kennedy ordered a naval blockade. The Cold War came closest to nuclear war before the Soviet Union removed the missiles.\n' > "$out/Cuban missile crisis.md"
printf 'Stalin blockaded West Berlin in 1948. The USA and Britain responded with the Berlin Airlift. The blockade was an early crisis of the Cold War between the Soviet Union and the West, and led to NATO.\n' > "$out/History/berlin blockade cold war.txt"
docx "$out/cell structure.docx" "Cell structure" "Animal cells have a nucleus, cytoplasm, cell membrane and mitochondria. Plant cells also have a cell wall, chloroplasts and a permanent vacuole."
printf 'Mitosis is cell division that makes two identical diploid cells. Stages are prophase, metaphase, anaphase and telophase. The nucleus divides.\n' > "$out/mitosis revision.txt"
printf 'Diffusion is the spread of particles from high to low concentration. Osmosis is the diffusion of water across a partially permeable cell membrane.\n' > "$out/osmosis and diffusion.txt"
docx "$out/EPQ proposal.docx" "EPQ: Will AI replace teachers?" "My EPQ asks whether artificial intelligence will replace teachers. I will research AI tutoring, large language models and how schools use AI."
printf 'EPQ log: read three articles on AI in education. AI tutoring systems give feedback. Large language models can write essays, which worries teachers.\n' > "$out/EPQ AI research log.txt"
docx "$out/EPQ literature review.docx" "Literature review" "This EPQ literature review covers artificial intelligence in schools. Studies show AI tutoring helps practice but teachers matter for motivation."
printf "Macbeth's ambition drives him to kill Duncan. Lady Macbeth questions his manhood. The witches' prophecies tempt Macbeth. Shakespeare shows guilt through blood imagery.\n" > "$out/macbeth themes.txt"
printf 'Lady Macbeth calls on spirits to unsex her. She plans Duncan’s murder but later sleepwalks, haunted by guilt. Shakespeare presents her decline.\n' > "$out/lady macbeth character.txt"
printf 'The three witches open Macbeth with paradox. Their prophecies about Macbeth becoming king spark his ambition.\n' > "$out/macbeth witches.txt"
printf 'Milk, eggs, bread, stamps, phone charger.\n' > "$out/Misc/shopping list.txt"
printf 'Tuesday 4pm with Dave. Practise roundabouts and parallel parking.\n' > "$out/Misc/driving lesson times.txt"
echo "made $(find "$out" -type f | wc -l) documents in $out"
