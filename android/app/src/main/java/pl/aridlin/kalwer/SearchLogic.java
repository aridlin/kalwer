package pl.aridlin.kalwer;

import java.math.BigDecimal;
import java.text.Normalizer;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Android-independent query rules, also exercised by the host regression suite. */
public final class SearchLogic {
    private SearchLogic() {}
    private static final Pattern SPACES = Pattern.compile("\\s+");
    private static final Pattern TIMER = Pattern.compile("([0-9]+)(h|m|s)");
    private static final Pattern NUMBER = Pattern.compile("[0-9]+");
    private static final Pattern EXPRESSION = Pattern.compile("[0-9.\\s+*/^()%a-z-]+");

    public static String normalize(String value) {
        String lower = value.toLowerCase(Locale.ROOT);
        boolean ascii = true;
        for (int i = 0; i < lower.length(); i++) if (lower.charAt(i) > 127) { ascii = false; break; }
        if (ascii) return lower;
        String decomposed = Normalizer.normalize(lower, Normalizer.Form.NFD);
        StringBuilder result = new StringBuilder(decomposed.length());
        for (int i = 0; i < decomposed.length(); i++) {
            char c = decomposed.charAt(i);
            int type = Character.getType(c);
            if (type != Character.NON_SPACING_MARK && type != Character.COMBINING_SPACING_MARK
                    && type != Character.ENCLOSING_MARK) result.append(c == 'ł' ? 'l' : c);
        }
        return result.toString();
    }

    public static final class Query {
        public final String text;
        final String[] words;
        public Query(String input) { text = normalize(input.trim()); words = SPACES.split(text); }
    }

    /** -1 means no match. Smaller scores rank first; all words must match. */
    public static int score(String label, String identifier, String query) {
        return scoreNormalized(normalize(label), normalize(identifier), new Query(query));
    }

    /** Hot path: app keys and query are prepared once, never inside the sort comparator. */
    public static int scoreNormalized(String name, String id, Query query) {
        String q = query.text;
        if (q.isEmpty() || name.equals(q)) return 0;
        if (name.startsWith(q)) return 1;
        if (name.contains(q)) return 2;
        int total = 0;
        for (String word : query.words) {
            if (name.contains(word)) { total += 3; continue; }
            if (id.contains(word)) { total += 5; continue; }
            // Short abbreviations can fuzzy-match names; prose must not match random letters.
            if (word.length() > 5 || q.contains(" ")) return -1;
            int at = 0;
            for (int i = 0; i < name.length() && at < word.length(); i++) {
                if (name.charAt(i) == word.charAt(at)) at++;
            }
            if (at != word.length()) return -1;
            total += 12;
        }
        return total;
    }

    public static String googleQuery(String query) {
        String q = query.trim();
        return q.startsWith("?") ? q.substring(1).trim() : q;
    }

    /** Bare numbers mean minutes; explicit h/m/s units may be combined. */
    public static int timerSeconds(String input) {
        String s = SPACES.matcher(input.toLowerCase(Locale.ROOT)).replaceAll("");
        if (NUMBER.matcher(s).matches()) s += "m";
        Matcher m = TIMER.matcher(s);
        long seconds = 0;
        int end = 0;
        try {
            while (m.find()) {
                if (m.start() != end) return -1;
                long n = Long.parseLong(m.group(1));
                if (n > 86400) return -1;
                seconds += n * (m.group(2).equals("h") ? 3600 : m.group(2).equals("m") ? 60 : 1);
                end = m.end();
            }
        } catch (NumberFormatException e) { return -1; }
        return end == s.length() && seconds > 0 && seconds <= 86400 ? (int) seconds : -1;
    }

    /** Returns null for ordinary text, incomplete expressions, or non-finite results. */
    public static String calculate(String input) {
        String s = input.trim().toLowerCase(Locale.ROOT).replace('×', '*').replace('÷', '/')
                .replace('−', '-').replace("√", "sqrt");
        if (s.startsWith("=")) s = s.substring(1).trim();
        if (s.isEmpty() || s.length() > 256 || !EXPRESSION.matcher(s).matches()) return null;
        try {
            Parser p = new Parser(s);
            double value = p.sum();
            p.space();
            if (p.pos != s.length() || !Double.isFinite(value)) return null;
            if (value == 0) return "0";
            return BigDecimal.valueOf(value).round(new java.math.MathContext(12))
                    .stripTrailingZeros().toPlainString();
        } catch (IllegalArgumentException e) { return null; }
    }

    private static final class Parser {
        final String s;
        int pos;
        Parser(String s) { this.s = s; }
        void space() { while (pos < s.length() && Character.isWhitespace(s.charAt(pos))) pos++; }
        boolean take(char c) { space(); if (pos < s.length() && s.charAt(pos) == c) { pos++; return true; } return false; }
        double sum() {
            double a = product();
            while (true) { if (take('+')) a += product(); else if (take('-')) a -= product(); else return a; }
        }
        double product() {
            double a = unary();
            while (true) { if (take('*')) a *= unary(); else if (take('/')) a /= unary(); else return a; }
        }
        double unary() { if (take('+')) return unary(); if (take('-')) return -unary(); return power(); }
        double power() { double a = atom(); while (take('%')) a /= 100; return take('^') ? Math.pow(a, unary()) : a; }
        double atom() {
            space();
            if (s.startsWith("sqrt", pos)) { pos += 4; return Math.sqrt(atom()); }
            if (take('(')) { double a = sum(); if (!take(')')) throw new IllegalArgumentException(); return a; }
            int start = pos;
            while (pos < s.length() && (Character.isDigit(s.charAt(pos)) || s.charAt(pos) == '.')) pos++;
            if (start == pos) throw new IllegalArgumentException();
            return Double.parseDouble(s.substring(start, pos));
        }
    }
}
