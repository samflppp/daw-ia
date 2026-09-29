#include "daw/domain/generation/Constraints.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace daw::domain::generation
{
namespace
{

constexpr std::array<std::string_view, 12> englishNames{
    "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

// Plain char and not u8: the domain carries UTF-8 bytes and never decodes them.
constexpr std::array<std::string_view, 12> frenchNames{
    "Do", "Do#", "Ré", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si"};

[[nodiscard]] std::string lowered(std::string_view text)
{
    std::string out{text};
    for (auto& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// "am", "f#m", "bbm", "c", "eb", "amin", "cmaj". Lower case already.
[[nodiscard]] std::optional<Key> parseKey(std::string_view word)
{
    if (word.empty())
        return std::nullopt;

    static constexpr std::array<std::pair<char, int>, 7> letters{
        {{'c', 0}, {'d', 2}, {'e', 4}, {'f', 5}, {'g', 7}, {'a', 9}, {'b', 11}}};

    int tonic = -1;
    for (const auto& [letter, pitchClass] : letters)
    {
        if (word.front() == letter)
            tonic = pitchClass;
    }
    if (tonic < 0)
        return std::nullopt;

    auto rest = word.substr(1);
    if (!rest.empty() && rest.front() == '#')
    {
        tonic += 1;
        rest.remove_prefix(1);
    }
    else if (!rest.empty() && rest.front() == 'b')
    {
        tonic += 11;
        rest.remove_prefix(1);
    }

    Key key{};
    key.tonic = tonic % 12;
    if (rest.empty() || rest == "maj")
        key.mode = Mode::major;
    else if (rest == "m" || rest == "min")
        key.mode = Mode::minor;
    else
        return std::nullopt;

    return key;
}

// The key said in French, over one to three words: "fa dièse mineur",
// "si bémol majeur", "la mineur", "fa# mineur", "mib majeur", "fa dièse". A
// note name alone is not a key: "la" and "si" are also plain French words,
// and "la mélodie" asks for no key. It takes a mode word, or an accidental
// word, which leaves no doubt. Lower case already. The key and how many words
// it took.
[[nodiscard]] std::optional<std::pair<Key, std::size_t>> parseFrenchKey(const std::vector<std::string>& words,
                                                                        std::size_t at)
{
    static constexpr std::array<std::pair<std::string_view, int>, 8> notes{
        {{"do", 0}, {"ré", 2}, {"re", 2}, {"mi", 4}, {"fa", 5}, {"sol", 7}, {"la", 9}, {"si", 11}}};

    std::string_view word = words[at];
    int tonic = -1;
    for (const auto& [name, pitchClass] : notes)
    {
        if (word.substr(0, name.size()) == name)
        {
            tonic = pitchClass;
            word.remove_prefix(name.size());
            break;
        }
    }
    if (tonic < 0)
        return std::nullopt;

    // "fa#", "sib", "fa#m" in one word; anything else glued to the name is
    // another word.
    auto sure = false;
    auto minorGlued = false;
    if (!word.empty() && word.front() == '#')
    {
        tonic += 1;
        sure = true;
        word.remove_prefix(1);
    }
    else if (!word.empty() && word.front() == 'b')
    {
        tonic += 11;
        sure = true;
        word.remove_prefix(1);
    }
    if (word == "m")
    {
        minorGlued = true;
        sure = true;
    }
    else if (!word.empty())
        return std::nullopt;

    auto next = at + 1;
    const auto isWord = [&](std::initializer_list<std::string_view> accepted)
    {
        return next < words.size() &&
               std::find(accepted.begin(), accepted.end(), std::string_view{words[next]}) != accepted.end();
    };

    if (minorGlued)
        return std::pair{Key{tonic % 12, Mode::minor}, std::size_t{1}};

    if (!sure && isWord({"dièse", "diese", "#"}))
    {
        tonic += 1;
        sure = true;
        ++next;
    }
    else if (!sure && isWord({"bémol", "bemol", "b"}))
    {
        tonic += 11;
        sure = true;
        ++next;
    }

    Key key{};
    key.tonic = tonic % 12;
    key.mode = Mode::major;
    if (isWord({"mineur", "mineure", "minor", "min", "m"}))
    {
        key.mode = Mode::minor;
        ++next;
    }
    else if (isWord({"majeur", "majeure", "major", "maj"}))
        ++next;
    else if (!sure)
        return std::nullopt;

    return std::pair{key, next - at};
}

template <typename T>
struct Word
{
    std::string_view text;
    T value;
};

constexpr std::array<Word<Resolution>, 13> resolutionWords{{
    {"noire", Resolution::quarter},
    {"noires", Resolution::quarter},
    {"1/4", Resolution::quarter},
    {"croche", Resolution::eighth},
    {"croches", Resolution::eighth},
    {"1/8", Resolution::eighth},
    {"double", Resolution::sixteenth},
    {"doubles", Resolution::sixteenth},
    {"double-croche", Resolution::sixteenth},
    {"doubles-croches", Resolution::sixteenth},
    {"double-croches", Resolution::sixteenth},
    {"1/16", Resolution::sixteenth},
    {"16e", Resolution::sixteenth},
}};

constexpr std::array<Word<Density>, 10> densityWords{{
    {"clair", Density::sparse},
    {"claire", Density::sparse},
    {"aéré", Density::sparse},
    {"aere", Density::sparse},
    {"léger", Density::sparse},
    {"leger", Density::sparse},
    {"moyen", Density::medium},
    {"moyenne", Density::medium},
    {"dense", Density::dense},
    {"chargé", Density::dense},
}};

constexpr std::array<Word<Register>, 10> registerWords{{
    {"grave", Register::low},
    {"graves", Register::low},
    {"medium", Register::mid},
    {"médium", Register::mid},
    {"milieu", Register::mid},
    {"aigu", Register::high},
    {"aigus", Register::high},
    {"aiguë", Register::high},
    {"aigue", Register::high},
    {"haut", Register::high},
}};

constexpr std::array<Word<Role>, 14> roleWords{{
    {"mélodie", Role::melody},
    {"melodie", Role::melody},
    {"lead", Role::melody},
    {"topline", Role::melody},
    {"basse", Role::bass},
    {"bass", Role::bass},
    {"808", Role::bass},
    {"accords", Role::chords},
    {"accord", Role::chords},
    {"chords", Role::chords},
    {"rythme", Role::rhythm},
    {"rythmique", Role::rhythm},
    {"drums", Role::rhythm},
    {"batterie", Role::rhythm},
}};

// Lower case already: "AABA" arrives as "aaba". The prime is typed three ways.
constexpr std::array<Word<Form>, 14> formWords{{
    {"aaba", Form::aaba},
    {"aaab", Form::aaab},
    {"aab", Form::aab},
    {"aa'", Form::aaPrime},
    {"aa\xE2\x80\xB2", Form::aaPrime}, // AA′
    {"aa\xE2\x80\x99", Form::aaPrime}, // AA’
    {"boucle", Form::loop},
    {"loop", Form::loop},
    {"répété", Form::loop},
    {"varié", Form::varied},
    {"varie", Form::varied},
    {"variée", Form::varied},
    {"libre", Form::free},
    {"free", Form::free},
}};

template <typename T, std::size_t N>
[[nodiscard]] std::optional<T> lookup(const std::array<Word<T>, N>& words, std::string_view word)
{
    for (const auto& candidate : words)
    {
        if (candidate.text == word)
            return candidate.value;
    }
    return std::nullopt;
}

// Sets a field, and reports it when the field was already set to another value.
template <typename T>
void assign(std::optional<T>& field,
            T value,
            std::string_view word,
            std::optional<std::string>& firstWord,
            Interpretation& out)
{
    if (field.has_value() && !(*field == value) && firstWord.has_value())
        out.conflicts.push_back(*firstWord + " puis " + std::string{word} + " : " + std::string{word} +
                                " retenu");
    field = value;
    firstWord = std::string{word};
}

// --- Value forms ------------------------------------------------------------

[[nodiscard]] std::string_view resolutionText(Resolution value) noexcept
{
    switch (value)
    {
    case Resolution::quarter:
        return "1/4";
    case Resolution::eighth:
        return "1/8";
    case Resolution::sixteenth:
        return "1/16";
    }
    return "1/16";
}

[[nodiscard]] std::string_view densityText(Density value) noexcept
{
    switch (value)
    {
    case Density::sparse:
        return "sparse";
    case Density::medium:
        return "medium";
    case Density::dense:
        return "dense";
    }
    return "medium";
}

[[nodiscard]] std::string_view registerText(Register value) noexcept
{
    switch (value)
    {
    case Register::low:
        return "low";
    case Register::mid:
        return "mid";
    case Register::high:
        return "high";
    }
    return "mid";
}

[[nodiscard]] std::string_view roleText(Role value) noexcept
{
    switch (value)
    {
    case Role::melody:
        return "melody";
    case Role::bass:
        return "bass";
    case Role::chords:
        return "chords";
    case Role::rhythm:
        return "rhythm";
    }
    return "melody";
}

[[nodiscard]] std::string_view formText(Form value) noexcept
{
    switch (value)
    {
    case Form::free:
        return "free";
    case Form::loop:
        return "loop";
    case Form::varied:
        return "varied";
    case Form::aaPrime:
        return "aa'";
    case Form::aab:
        return "aab";
    case Form::aaba:
        return "aaba";
    case Form::aaab:
        return "aaab";
    }
    return "free";
}

template <typename T, std::size_t N>
[[nodiscard]] Result<std::optional<T>> optionalEnum(const Value& value,
                                                    std::string_view key,
                                                    const std::array<T, N>& all,
                                                    std::string_view (*text)(T) noexcept)
{
    const auto* found = value.find(key);
    if (found == nullptr || found->isNull())
        return std::optional<T>{};

    auto word = found->asString();
    if (!word)
        return fail(ErrorCode::invalidPayload, std::string{key} + ": " + word.error().message);

    for (const auto candidate : all)
    {
        if (text(candidate) == word.value())
            return std::optional<T>{candidate};
    }
    return fail(ErrorCode::invalidPayload, std::string{key} + ": unknown value \"" + word.value() + "\"");
}

[[nodiscard]] Result<std::vector<std::string>> strings(const Value& value, std::string_view key)
{
    std::vector<std::string> out;
    const auto* found = value.find(key);
    if (found == nullptr || found->isNull())
        return out;

    const auto* items = found->asArray();
    if (items == nullptr)
        return fail(ErrorCode::invalidPayload, std::string{key} + " must be an array");

    for (const auto& item : *items)
    {
        auto text = item.asString();
        if (!text)
            return fail(ErrorCode::invalidPayload, std::string{key} + ": " + text.error().message);
        out.push_back(std::move(text).value());
    }
    return out;
}

[[nodiscard]] Value stringArray(const std::vector<std::string>& items)
{
    Value::Array out;
    for (const auto& item : items)
        out.emplace_back(item);
    return Value::array(std::move(out));
}

} // namespace

std::string_view tonicName(int pitchClass) noexcept
{
    return englishNames[static_cast<std::size_t>(((pitchClass % 12) + 12) % 12)];
}

// --- Constraints -------------------------------------------------------------

Value Constraints::toValue() const
{
    Value::Object members;
    if (key.has_value())
    {
        members.emplace_back("key",
                             Value::object({{"tonic", Value{tonicName(key->tonic)}},
                                            {"mode", Value{key->mode == Mode::major ? "major" : "minor"}}}));
    }
    if (resolution.has_value())
        members.emplace_back("resolution", Value{resolutionText(*resolution)});
    if (density.has_value())
        members.emplace_back("density", Value{densityText(*density)});
    if (reg.has_value())
        members.emplace_back("register", Value{registerText(*reg)});
    if (role.has_value())
        members.emplace_back("role", Value{roleText(*role)});
    if (form.has_value())
        members.emplace_back("form", Value{formText(*form)});
    return Value::object(std::move(members));
}

Result<Constraints> Constraints::fromValue(const Value& value)
{
    if (!value.isObject())
        return fail(ErrorCode::invalidPayload, "constraints must be an object");

    Constraints out{};

    if (const auto* key = value.find("key"); key != nullptr && !key->isNull())
    {
        auto tonic = key->stringAt("tonic");
        auto mode = key->stringAt("mode");
        if (!tonic || !mode)
            return fail(ErrorCode::invalidPayload, "key needs a tonic and a mode");
        if (mode.value() != "major" && mode.value() != "minor")
            return fail(ErrorCode::invalidPayload, "key.mode: unknown value \"" + mode.value() + "\"");

        auto parsed = parseKey(lowered(tonic.value()));
        if (!parsed.has_value())
            return fail(ErrorCode::invalidPayload, "key.tonic: unknown value \"" + tonic.value() + "\"");

        parsed->mode = mode.value() == "major" ? Mode::major : Mode::minor;
        out.key = parsed;
    }

    auto resolution =
        optionalEnum<Resolution, 3>(value,
                                    "resolution",
                                    {Resolution::quarter, Resolution::eighth, Resolution::sixteenth},
                                    resolutionText);
    if (!resolution)
        return resolution.error();
    out.resolution = resolution.value();

    auto density = optionalEnum<Density, 3>(
        value, "density", {Density::sparse, Density::medium, Density::dense}, densityText);
    if (!density)
        return density.error();
    out.density = density.value();

    auto reg = optionalEnum<Register, 3>(
        value, "register", {Register::low, Register::mid, Register::high}, registerText);
    if (!reg)
        return reg.error();
    out.reg = reg.value();

    auto role = optionalEnum<Role, 4>(
        value, "role", {Role::melody, Role::bass, Role::chords, Role::rhythm}, roleText);
    if (!role)
        return role.error();
    out.role = role.value();

    auto form = optionalEnum<Form, 7>(
        value,
        "form",
        {Form::free, Form::loop, Form::varied, Form::aaPrime, Form::aab, Form::aaba, Form::aaab},
        formText);
    if (!form)
        return form.error();
    out.form = form.value();

    return out;
}

// --- Interpretation ----------------------------------------------------------

Value Interpretation::toValue() const
{
    auto out = constraints.toValue();
    static_cast<void>(out.set("ignored", stringArray(ignored)));
    static_cast<void>(out.set("conflicts", stringArray(conflicts)));
    return out;
}

Result<Interpretation> Interpretation::fromValue(const Value& value)
{
    auto constraints = Constraints::fromValue(value);
    if (!constraints)
        return constraints.error();

    auto ignored = strings(value, "ignored");
    if (!ignored)
        return ignored.error();

    auto conflicts = strings(value, "conflicts");
    if (!conflicts)
        return conflicts.error();

    Interpretation out{};
    out.constraints = std::move(constraints).value();
    out.ignored = std::move(ignored).value();
    out.conflicts = std::move(conflicts).value();
    return out;
}

// --- the local interpreter ---------------------------------------------------

Interpretation LocalInterpreter::parse(std::string_view text)
{
    Interpretation out{};
    std::optional<std::string> keyWord;
    std::optional<std::string> resolutionWord;
    std::optional<std::string> densityWord;
    std::optional<std::string> registerWord;
    std::optional<std::string> roleWord;
    std::optional<std::string> formWord;

    // The words first, then read with a look ahead: a key said in French
    // spans several of them.
    std::vector<std::string_view> originals;
    std::vector<std::string> words;
    std::size_t at = 0;
    while (at < text.size())
    {
        while (at < text.size() && (text[at] == ' ' || text[at] == '	' || text[at] == ','))
            ++at;
        auto end = at;
        while (end < text.size() && text[end] != ' ' && text[end] != '	' && text[end] != ',')
            ++end;
        if (end == at)
            break;

        originals.push_back(text.substr(at, end - at));
        words.push_back(lowered(originals.back()));
        at = end;
    }

    for (std::size_t index = 0; index < words.size(); ++index)
    {
        const auto original = originals[index];
        const auto& word = words[index];

        if (auto french = parseFrenchKey(words, index); french.has_value())
        {
            const auto last = index + french->second - 1;
            const auto said = text.substr(static_cast<std::size_t>(original.data() - text.data()),
                                          static_cast<std::size_t>(originals[last].data() - original.data()) +
                                              originals[last].size());
            assign(out.constraints.key, french->first, said, keyWord, out);
            index = last;
        }
        else if (auto role = lookup(roleWords, word); role.has_value())
            assign(out.constraints.role, *role, original, roleWord, out);
        else if (auto resolution = lookup(resolutionWords, word); resolution.has_value())
            assign(out.constraints.resolution, *resolution, original, resolutionWord, out);
        else if (auto density = lookup(densityWords, word); density.has_value())
            assign(out.constraints.density, *density, original, densityWord, out);
        else if (auto reg = lookup(registerWords, word); reg.has_value())
            assign(out.constraints.reg, *reg, original, registerWord, out);
        else if (auto form = lookup(formWords, word); form.has_value())
            assign(out.constraints.form, *form, original, formWord, out);
        else if (auto key = parseKey(word); key.has_value())
            assign(out.constraints.key, *key, original, keyWord, out);
        else
            out.ignored.emplace_back(original);
    }

    return out;
}

void LocalInterpreter::interpret(std::string_view text, std::function<void(Interpretation)> done)
{
    if (done)
        done(parse(text));
}

// --- words -------------------------------------------------------------------

std::string describe(Key key)
{
    return std::string{frenchNames[static_cast<std::size_t>(((key.tonic % 12) + 12) % 12)]} +
           (key.mode == Mode::major ? " majeur" : " mineur");
}

std::string_view describe(Resolution resolution) noexcept
{
    switch (resolution)
    {
    case Resolution::quarter:
        return "noires";
    case Resolution::eighth:
        return "croches";
    case Resolution::sixteenth:
        return "doubles";
    }
    return "doubles";
}

std::string_view describe(Density density) noexcept
{
    switch (density)
    {
    case Density::sparse:
        return "clair";
    case Density::medium:
        return "moyen";
    case Density::dense:
        return "dense";
    }
    return "moyen";
}

std::string_view describe(Register reg) noexcept
{
    switch (reg)
    {
    case Register::low:
        return "grave";
    case Register::mid:
        return "medium";
    case Register::high:
        return "aigu";
    }
    return "medium";
}

std::string_view describe(Role role) noexcept
{
    switch (role)
    {
    case Role::melody:
        return "mélodie";
    case Role::bass:
        return "basse";
    case Role::chords:
        return "accords";
    case Role::rhythm:
        return "rythme";
    }
    return "mélodie";
}

std::string_view describe(Form form) noexcept
{
    switch (form)
    {
    case Form::free:
        return "libre";
    case Form::loop:
        return "boucle";
    case Form::varied:
        return "varié";
    case Form::aaPrime:
        return "AA\xE2\x80\xB2";
    case Form::aab:
        return "AAB";
    case Form::aaba:
        return "AABA";
    case Form::aaab:
        return "AAAB";
    }
    return "libre";
}

std::string_view describe(Source source) noexcept
{
    switch (source)
    {
    case Source::imposed:
        return "imposé";
    case Source::deduced:
        return "déduit";
    case Source::defaulted:
        return "défaut";
    }
    return "défaut";
}

std::string describe(const ResolvedConstraints& constraints)
{
    const auto part = [](std::string text, Source source)
    { return text + " (" + std::string{describe(source)} + ")"; };

    std::string out = part(describe(constraints.key.value), constraints.key.source);
    // A rhythm channel plays its own pitch: its key and register say nothing.
    if (constraints.role.value == Role::rhythm)
        out = {};
    else
        out += " · ";

    out += part(std::string{describe(constraints.resolution.value)}, constraints.resolution.source);
    out += " · " + part(std::string{describe(constraints.density.value)}, constraints.density.source);
    if (constraints.role.value != Role::rhythm)
        out += " · " + part(std::string{describe(constraints.reg.value)}, constraints.reg.source);
    out += " · " + part(std::string{describe(constraints.role.value)}, constraints.role.source);
    out += " · " + part(std::string{describe(constraints.form.value)}, constraints.form.source);
    return out;
}

std::string shortLabel(const ResolvedConstraints& constraints)
{
    std::string out;
    const auto add = [&out](std::string_view word)
    {
        if (!out.empty())
            out += ' ';
        out += word;
    };

    if (constraints.key.source == Source::imposed)
        add(std::string{tonicName(constraints.key.value.tonic)} +
            (constraints.key.value.mode == Mode::minor ? "m" : ""));
    if (constraints.resolution.source == Source::imposed)
        add(describe(constraints.resolution.value));
    if (constraints.density.source == Source::imposed)
        add(describe(constraints.density.value));
    if (constraints.reg.source == Source::imposed)
        add(describe(constraints.reg.value));
    add(describe(constraints.role.value));
    if (constraints.form.source == Source::imposed)
        add(describe(constraints.form.value));
    return out;
}

} // namespace daw::domain::generation
