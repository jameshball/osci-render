#include "../../Source/motion/model/SharedList.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

static void check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

struct Item {
    int id = 0;
    std::string name;
    bool operator==(const Item&) const = default;
};

int main() {
    motion::SharedList<Item> list {{1, "a"}, {2, "b"}, {3, "c"}};
    check(list.size() == 3 && list[1].name == "b" && list.front().id == 1 && list.back().id == 3, "Reads look like a vector's");
    check(std::find_if(list.begin(), list.end(), [](const Item& item) { return item.id == 2; }) - list.begin() == 1, "Iterators work with standard algorithms");

    // A copy shares every item until one is changed.
    auto copy = list;
    for (std::size_t index = 0; index < list.size(); ++index) { check(copy.shares(index, list), "A copy shares its items"); }
    const auto* before = &list[1];
    copy.change(1).name = "changed";
    check(list[1].name == "b" && &list[1] == before, "Changing a copy leaves the original untouched");
    check(copy[1].name == "changed", "The copy sees its change");
    check(copy.shares(0, list) && !copy.shares(1, list) && copy.shares(2, list), "Only the changed item stops being shared");

    // An item nothing else holds is changed in place.
    const auto* owned = &copy[1];
    copy.change(1).name = "again";
    check(&copy[1] == owned, "An unshared item is not copied again");

    // changeEach copies only the items it picks.
    auto picked = list;
    picked.changeEach([](const Item& item) { return item.id != 2; }, [](Item& item) { item.name += "!"; });
    check(picked[0].name == "a!" && picked[2].name == "c!" && picked.shares(1, list) && !picked.shares(0, list), "changeEach leaves unpicked items shared");
    check(picked.changeById(3) == &picked[2] && picked.changeById(9) == nullptr, "changeById finds the item to change");

    // itemAt reads from a const list and changes through a mutable one.
    const auto& constant = list;
    check(&motion::itemAt(constant, 0) == &list[0], "itemAt reads a const list without copying");
    auto viaItemAt = list;
    motion::itemAt(viaItemAt, 0).name = "z";
    check(list[0].name == "a" && viaItemAt[0].name == "z", "itemAt changes a mutable list's own copy");

    // Structure edits move pointers, never items.
    auto edited = list;
    edited.move(0, 2);
    check(edited[2].id == 1 && edited.shares(2, list), "Moving keeps the item shared");
    edited.erase(edited.begin());
    edited.insert(edited.begin(), {4, "d"});
    edited.push_back({5, "e"});
    check(edited.size() == 4 && edited[0].id == 4 && edited.back().id == 5, "Insert, erase and push_back");
    check(edited.eraseIf([](const Item& item) { return item.id > 3; }) == 2 && edited.size() == 2, "eraseIf removes matches");
    auto grown = list;
    grown.resize(5, {9, "pad"});
    check(grown.size() == 5 && grown[3].id == 9 && grown[4].id == 9 && grown.shares(0, list), "resize pads and keeps existing items shared");
    grown.change(3).name = "own";
    check(grown[4].name == "pad", "Padded items are copied apart when one changes");

    // Equality compares contents, fast when items are shared.
    auto same = list;
    check(same == list, "Shared lists are equal");
    same.change(0);
    check(same == list, "A changed-but-identical item still compares equal");
    same.change(0).name = "x";
    check(!(same == list), "Different contents compare unequal");

    std::cout << "SharedList tests passed\n";
}
