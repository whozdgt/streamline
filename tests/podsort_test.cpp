// g++ -std=c++17 -O2 -Isrc src/podsort.cpp tests/podsort_test.cpp -o /tmp/podsort_test && /tmp/podsort_test
#include "podsort.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using podsort::Doc;
int main(int argc, char **argv)
{
    std::vector<Doc> d = {
        {"berlin conference notes", "The Berlin Conference 1884-85", "Bismarck called the Berlin Conference in 1884 so European powers could agree rules for claiming land in Africa. King Leopold of Belgium wanted the Congo Free State and Britain and France argued over West Africa. The conference set the principle of effective occupation.", ""},
        {"Leopold and the Congo", "Leopold II", "King Leopold II used the Berlin Conference to gain the Congo Free State as his personal colony. Rubber quotas and forced labour in the Congo killed millions. Leopold got recognition from the European powers at the conference.", ""},
        {"scramble for africa essay", "Scramble for Africa", "The Scramble for Africa saw European powers carve up Africa between 1881 and 1914. The Berlin Conference gave rules for effective occupation. Britain, France, Germany, Belgium and Portugal claimed colonies across Africa.", ""},
        {"mindmap berlin conf outcome", "", "Outcomes of the Berlin Conference: free trade on the Congo and Niger rivers, effective occupation, Leopold recognised in the Congo Free State, Africa partitioned by European powers.", ""},
        {"cold war origins", "Origins of the Cold War", "The Cold War began after 1945 as the USA and the Soviet Union disagreed at Yalta and Potsdam. Stalin wanted a buffer zone in Eastern Europe. Truman responded with the Truman Doctrine and containment.", ""},
        {"Cuban missile crisis", "Cuban Missile Crisis 1962", "In 1962 Khrushchev placed nuclear missiles in Cuba. Kennedy ordered a naval blockade. The Cold War came closest to nuclear war before the Soviet Union removed the missiles.", ""},
        {"berlin blockade cold war", "Berlin Blockade", "Stalin blockaded West Berlin in 1948. The USA and Britain responded with the Berlin Airlift. The blockade was an early crisis of the Cold War between the Soviet Union and the West, and led to NATO.", ""},
        {"containment and truman doctrine", "", "Truman Doctrine 1947 promised support for countries resisting communism. Marshall Plan aid rebuilt Western Europe. Containment shaped US policy against the Soviet Union in the Cold War.", ""},
        {"cell structure", "Cell structure", "Animal cells have a nucleus, cytoplasm, cell membrane and mitochondria. Plant cells also have a cell wall, chloroplasts and a permanent vacuole. Ribosomes make proteins.", ""},
        {"mitosis revision", "Mitosis", "Mitosis is cell division that makes two identical diploid cells. Stages are prophase, metaphase, anaphase and telophase. The nucleus divides and chromosomes are copied.", ""},
        {"osmosis and diffusion", "Transport in cells", "Diffusion is the spread of particles from high to low concentration. Osmosis is the diffusion of water across a partially permeable cell membrane. Active transport uses energy from respiration in the cell.", ""},
        {"ionic bonding", "Ionic bonding", "Ionic bonding happens between metals and non-metals. Electrons are transferred to make positive and negative ions held by electrostatic forces. Sodium chloride is a giant ionic lattice.", ""},
        {"covalent bonding", "Covalent bonds", "Covalent bonding shares pairs of electrons between non-metal atoms. Simple molecules like water have low melting points. Giant covalent structures like diamond have high melting points.", ""},
        {"metallic bonding chem", "Metallic bonding", "Metallic bonding is the attraction between positive metal ions and delocalised electrons. Metals conduct electricity because the delocalised electrons move. Alloys are harder.", ""},
        {"EPQ proposal", "EPQ: Will AI replace teachers?", "My EPQ asks whether artificial intelligence will replace teachers. I will research AI tutoring, large language models and how schools use AI. Sources include journal articles on AI in education.", ""},
        {"EPQ AI research log", "Research log", "EPQ log: read three articles on AI in education. AI tutoring systems give feedback. Large language models can write essays, which worries teachers. Next: survey students about AI.", ""},
        {"EPQ literature review", "Literature review", "This EPQ literature review covers artificial intelligence in schools. Studies show AI tutoring helps practice but teachers matter for motivation. Large language models raise questions on cheating.", ""},
        {"AI ethics EPQ", "Ethics of AI", "EPQ section on ethics: AI bias, privacy of student data, and whether AI should mark work. Artificial intelligence in education must keep teachers in control.", ""},
        {"macbeth themes", "Macbeth: ambition", "Macbeth's ambition drives him to kill Duncan. Lady Macbeth questions his manhood. The witches' prophecies tempt Macbeth. Shakespeare shows guilt through blood imagery.", ""},
        {"lady macbeth character", "Lady Macbeth", "Lady Macbeth calls on spirits to unsex her. She plans Duncan's murder but later sleepwalks, haunted by guilt and blood. Shakespeare presents her decline.", ""},
        {"macbeth witches", "The witches", "The three witches open Macbeth with paradox: fair is foul. Their prophecies about Macbeth becoming king spark his ambition. Shakespeare's audience feared witchcraft.", ""},
        {"shopping list", "", "Milk, eggs, bread, stamps, phone charger.", ""},
        {"driving lesson times", "", "Tuesday 4pm with Dave. Practise roundabouts and parallel parking.", ""},
    };
    int detail = argc > 1 ? std::atoi(argv[1]) : 1;
    for (int det = 0; det < 3; ++det) {
        if (argc > 1 && det != detail) continue;
        auto topics = podsort::sortDocs(d, det);
        std::printf("=== detail %d: %zu topics\n", det, topics.size());
        for (auto &t : topics) {
            std::printf("[%s]", t.name.c_str());
            for (auto &k : t.keywords) std::printf(" · %s", k.c_str());
            std::printf("\n");
            for (int i : t.docs) std::printf("    %s\n", d[size_t(i)].title.c_str());
        }
    }
    // a new document finds its place
    std::vector<std::vector<int>> topics = {{0, 1, 2, 3}, {4, 5, 6, 7}, {8, 9, 10}};
    d.push_back({"berlin airlift", "", "The Berlin Airlift supplied West Berlin during Stalin's blockade in the Cold War. The USA flew in food.", ""});
    d.push_back({"photosynthesis", "", "Plant cells use chloroplasts for photosynthesis. Light energy makes glucose in the cell.", ""});
    auto p = podsort::placeNew(d, topics, {int(d.size()) - 2, int(d.size()) - 1}, 1);
    std::printf("placeNew: airlift -> %d (want 1), photosynthesis -> %d (want 2)\n", p[0], p[1]);
    // the balanced sort must find the six subjects and leave the two odd files out
    const auto t = podsort::sortDocs(std::vector<Doc>(d.begin(), d.end() - 2), 1);
    const bool ok = p[0] == 1 && p[1] == 2 && t.size() == 7 && t.back().other && t.back().docs.size() == 2;
    std::printf(ok ? "OK\n" : "FAILED\n");
    return ok ? 0 : 1;
}
