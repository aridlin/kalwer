package pl.aridlin.kalwer;

import java.util.*;

/** Repeatable desktop microbenchmark, not a claim about physical phone latency. */
public final class SearchBenchmark {
    private record Entry(String name, String id) {}
    private record Match(Entry entry, int score) {}
    public static void main(String[] args) {
        List<String> names = new ArrayList<>();
        for (int i = 0; i < 1000; i++) names.add("Application " + i + " Photos Maps Łódź");
        String[] queries = {"app", "maps", "photos", "lodz", "not found", "ap", "application 9"};
        List<Entry> entries = new ArrayList<>();
        for (String name : names) entries.add(new Entry(SearchLogic.normalize(name), SearchLogic.normalize("example." + name)));
        boolean prepared = args.length > 0 && args[0].equals("--prepared");
        long checksum = 0;
        for (int pass = 0; pass < 2; pass++) {
            long start = System.nanoTime();
            for (int repeat = 0; repeat < 8; repeat++) for (String query : queries) {
                if (prepared) {
                    SearchLogic.Query q = new SearchLogic.Query(query);
                    List<Match> matches = new ArrayList<>();
                    for (Entry entry : entries) {
                        int score = SearchLogic.scoreNormalized(entry.name, entry.id, q);
                        if (score >= 0) matches.add(new Match(entry, score));
                    }
                    matches.sort(Comparator.comparingInt(Match::score));
                    checksum += matches.size();
                    continue;
                }
                List<String> matched = new ArrayList<>();
                for (String name : names) if (SearchLogic.score(name, "example." + name, query) >= 0) matched.add(name);
                matched.sort(Comparator.comparingInt(n -> SearchLogic.score(n, "example." + n, query)));
                checksum += matched.size();
            }
            System.out.printf(Locale.ROOT, "pass=%d queries=56 apps=1000 elapsed_ms=%.2f checksum=%d%n", pass,
                    (System.nanoTime() - start) / 1e6, checksum);
        }
    }
}
