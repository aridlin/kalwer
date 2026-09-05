package pl.aridlin.kalwer;

/** Run with ./test-search.sh; deliberately needs no Android runtime or test downloads. */
public final class SearchLogicTest {
    private static int checks;
    private static void equal(Object expected, Object actual) {
        checks++;
        if (!java.util.Objects.equals(expected, actual)) throw new AssertionError("Expected " + expected + ", got " + actual);
    }
    private static void yes(boolean condition) { equal(true, condition); }
    public static void main(String[] args) {
        equal(0, SearchLogic.score("Firefox", "org.mozilla.firefox", "firefox"));
        yes(SearchLogic.score("Firefox", "org.mozilla.firefox", "fire") < SearchLogic.score("Firefox", "org.mozilla.firefox", "ff"));
        yes(SearchLogic.score("Łódź Maps", "maps", "lodz") >= 0);
        yes(SearchLogic.score("Google Maps", "com.google.maps", "google maps") >= 0);
        equal(-1, SearchLogic.score("Google Maps", "com.google.maps", "weather tomorrow"));
        equal(-1, SearchLogic.score("Chrome", "com.android.chrome", "chocolate cake recipes"));
        equal(-1, SearchLogic.score("Firefox", "org.mozilla.firefox", "zzzz"));
        equal("cats & dogs + Łódź", SearchLogic.googleQuery(" ? cats & dogs + Łódź "));
        equal("why is the sky blue", SearchLogic.googleQuery("why is the sky blue"));
        equal("", SearchLogic.googleQuery("? "));
        equal("14", SearchLogic.calculate("2 + 3 * 4"));
        equal("20", SearchLogic.calculate("(2 + 3) * 4"));
        equal("512", SearchLogic.calculate("2^3^2"));
        equal("-4", SearchLogic.calculate("-2^2"));
        equal("4", SearchLogic.calculate("(-2)^2"));
        equal("0.25", SearchLogic.calculate("2^-2"));
        equal("4", SearchLogic.calculate("sqrt(16)"));
        equal("7", SearchLogic.calculate("√49"));
        equal("10", SearchLogic.calculate("200*5%"));
        equal("0.3", SearchLogic.calculate("0.1+0.2"));
        equal("0", SearchLogic.calculate("-0"));
        equal("6", SearchLogic.calculate("= 2 × 3"));
        for (String s : new String[]{"", "? cats", "firefox", "1/0", "sqrt(-1)", "2+", "(2+3", "2..3", "2^9999", "1 2"}) equal(null, SearchLogic.calculate(s));
        equal(300, SearchLogic.timerSeconds("5"));
        equal(300, SearchLogic.timerSeconds("5m"));
        equal(5430, SearchLogic.timerSeconds("1h 30m 30s"));
        equal(86400, SearchLogic.timerSeconds("24h"));
        for (String s : new String[]{"", "0", "25h", "5minutes", "-1", "2m junk", "99999999999999999999999999h"}) equal(-1, SearchLogic.timerSeconds(s));
        System.out.println("Passed " + checks + " search, fallback, calculator and timer checks.");
    }
}
