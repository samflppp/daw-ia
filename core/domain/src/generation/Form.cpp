#include "daw/domain/generation/Form.h"

namespace daw::domain::generation
{

int unitBars(Role role, int rangeBars) noexcept
{
    // Eight bars of a melody in one-bar units would be the same bar eight
    // times: a topline breathes over two.
    return role == Role::melody && rangeBars >= 8 ? 2 : 1;
}

Form defaultForm(Role role, int units) noexcept
{
    if (units <= 1)
        return Form::free;

    switch (role)
    {
    case Role::rhythm:
    case Role::chords:
        return Form::loop;
    case Role::melody:
    case Role::bass:
        break;
    }

    if (units == 2)
        return Form::aaPrime;
    if (units == 3)
        return Form::aab;
    return role == Role::melody ? Form::aaba : Form::aaab;
}

std::vector<Letter> schema(Form form, int units)
{
    std::vector<Letter> out;
    if (form == Form::free || units <= 1)
        return out;

    for (int i = 0; i < units; ++i)
    {
        auto letter = Letter::a;
        switch (form)
        {
        case Form::free:
        case Form::loop:
            break;
        case Form::varied:
            letter = i == 0 ? Letter::a : Letter::aVaried;
            break;
        case Form::aaPrime:
            letter = i % 2 == 0 ? Letter::a : Letter::aVaried;
            break;
        case Form::aab:
            letter = i % 3 == 2 ? Letter::b : Letter::a;
            break;
        case Form::aaba:
            letter = i % 4 == 2 ? Letter::b : (i % 4 == 3 ? Letter::aReturn : Letter::a);
            break;
        case Form::aaab:
            letter = i % 4 == 3 ? Letter::b : Letter::a;
            break;
        }
        out.push_back(letter);
    }
    return out;
}

} // namespace daw::domain::generation
