/**
 * @file demo.cpp
 * @brief Real-world usage examples for BinarySet and BSSearcher
 *
 * Build:
 *   g++ -std=c++20 -Wall -Wextra -O2 -o demo demo.cpp
 *
 * Run:
 *   ./demo
 *
 * This file walks through three self-contained scenarios that show how
 * BinarySet and BSSearcher are used in practice:
 *
 *  Scenario 1 — Permission system
 *      A web application maps user roles to sets of permissions.
 *      We check whether a user has the rights needed to perform an action.
 *
 *  Scenario 2 — Recipe matcher
 *      A kitchen assistant holds a pantry (available ingredients) and a
 *      catalogue of recipes.  We find every recipe that can be made with
 *      what is currently in the pantry, using BSSearcher for fast lookup.
 *
 *  Scenario 3 — Course prerequisite validator
 *      A university registration system models completed courses as a set.
 *      Each offered course has a prerequisite set.  We find all courses a
 *      student is eligible to enrol in.
 */

#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "binary_set.hxx"

// ============================================================================
//  Tiny pretty-print helper
// ============================================================================

void section(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(60, '=') << "\n";
}

void subsection(const std::string& title) { std::cout << "\n  -- " << title << " --\n"; }

// ============================================================================
//  Scenario 1: Permission system
// ============================================================================
//
// The application defines a fixed universe of 16 permissions (0-15).
// Each permission is given a name for display purposes.
// Users are assigned a permission set.
// Actions require a specific set of permissions.
// We check whether a user's permissions are a superset of the required ones.

namespace permissions {

// The permission universe.  Each constant is an index into the BinarySet.
enum : unsigned int {
    READ = 0,
    WRITE = 1,
    DELETE = 2,
    ADMIN = 3,
    VIEW_REPORTS = 4,
    EXPORT_DATA = 5,
    MANAGE_USERS = 6,
    BILLING = 7,
    API_ACCESS = 8,
    AUDIT_LOG = 9,
    // Total: 10 permissions defined, capacity set to 16 for easy extension.
};

constexpr unsigned int CAPACITY = 16;

const std::unordered_map<unsigned int, std::string> NAMES = {
    {READ, "read"},
    {WRITE, "write"},
    {DELETE, "delete"},
    {ADMIN, "admin"},
    {VIEW_REPORTS, "view_reports"},
    {EXPORT_DATA, "export_data"},
    {MANAGE_USERS, "manage_users"},
    {BILLING, "billing"},
    {API_ACCESS, "api_access"},
    {AUDIT_LOG, "audit_log"},
};

// Pretty-print a permission set.
void print_perms(const std::string& label, const BinarySet& perms) {
    std::cout << "  " << std::left << std::setw(20) << label << ": { ";
    bool first = true;
    for (unsigned int p : perms) {
        if (!first) std::cout << ", ";
        auto it = NAMES.find(p);
        std::cout << (it != NAMES.end() ? it->second : std::to_string(p));
        first = false;
    }
    std::cout << " }\n";
}

void run() {
    section("Scenario 1 — Permission system");

    // -----------------------------------------------------------------------
    // Define user permission sets
    // -----------------------------------------------------------------------
    BinarySet guest(CAPACITY);
    guest.add(READ);

    BinarySet editor(CAPACITY);
    editor.add(READ);
    editor.add(WRITE);
    editor.add(VIEW_REPORTS);

    BinarySet moderator(CAPACITY);
    moderator.add(READ);
    moderator.add(WRITE);
    moderator.add(DELETE);
    moderator.add(VIEW_REPORTS);
    moderator.add(EXPORT_DATA);

    BinarySet superadmin(CAPACITY);
    for (unsigned int p = READ; p <= AUDIT_LOG; ++p) superadmin.add(p);

    subsection("User permission sets");
    print_perms("guest", guest);
    print_perms("editor", editor);
    print_perms("moderator", moderator);
    print_perms("superadmin", superadmin);

    // -----------------------------------------------------------------------
    // Define required permissions for various actions
    // -----------------------------------------------------------------------
    BinarySet req_view(CAPACITY);
    req_view.add(READ);

    BinarySet req_publish(CAPACITY);
    req_publish.add(READ);
    req_publish.add(WRITE);

    BinarySet req_delete_user(CAPACITY);
    req_delete_user.add(DELETE);
    req_delete_user.add(MANAGE_USERS);

    BinarySet req_billing_export(CAPACITY);
    req_billing_export.add(BILLING);
    req_billing_export.add(EXPORT_DATA);

    subsection("Access control checks  (required ⊆ user?)");

    struct Check {
        std::string user;
        const BinarySet& perms;
        std::string action;
        const BinarySet& required;
    };
    std::vector<Check> checks = {
        {"guest", guest, "view content", req_view},
        {"guest", guest, "publish post", req_publish},
        {"editor", editor, "publish post", req_publish},
        {"editor", editor, "delete user", req_delete_user},
        {"moderator", moderator, "delete user", req_delete_user},
        {"moderator", moderator, "billing export", req_billing_export},
        {"superadmin", superadmin, "billing export", req_billing_export},
        {"superadmin", superadmin, "delete user", req_delete_user},
    };

    for (auto& c : checks) {
        bool allowed = c.perms.superset_of(c.required);
        std::cout << "  " << std::left << std::setw(12) << c.user << std::setw(20) << c.action << " -> " << (allowed ? "ALLOWED" : "DENIED") << "\n";
    }

    // -----------------------------------------------------------------------
    // Compute which permissions editor is missing to become a moderator
    // -----------------------------------------------------------------------
    subsection("What does editor need to reach moderator level?");
    BinarySet missing = moderator - editor;  // set difference
    print_perms("missing", missing);

    // -----------------------------------------------------------------------
    // Revoke a permission from moderator and verify
    // -----------------------------------------------------------------------
    subsection("Revoking EXPORT_DATA from moderator");
    BinarySet revoked_export(CAPACITY);
    revoked_export.add(EXPORT_DATA);
    moderator -= revoked_export;
    print_perms("moderator (updated)", moderator);
    std::cout << "  Can still view reports? " << (moderator.superset_of(req_view) ? "yes" : "no") << "\n";

    // -----------------------------------------------------------------------
    // Intersection: shared permissions between editor and moderator
    // -----------------------------------------------------------------------
    subsection("Permissions shared by editor and (updated) moderator");
    BinarySet shared = editor & moderator;
    print_perms("shared", shared);
}

}  // namespace permissions

// ============================================================================
//  Scenario 2: Recipe matcher
// ============================================================================
//
// The pantry is a BinarySet over a universe of ingredients (0-based indices).
// Each recipe is a BinarySet of required ingredients.
// BSSearcher lets us find all recipes whose required ingredients
// are a subset of the current pantry in O(capacity × paths) time,
// without iterating over every recipe linearly.

namespace recipes {

// Ingredient universe
enum : unsigned int {
    FLOUR = 0,
    SUGAR = 1,
    BUTTER = 2,
    EGG = 3,
    MILK = 4,
    YEAST = 5,
    SALT = 6,
    BAKING_POWDER = 7,
    COCOA = 8,
    VANILLA = 9,
    OLIVE_OIL = 10,
    GARLIC = 11,
    PASTA = 12,
    TOMATO_SAUCE = 13,
    CHEESE = 14,
    CHICKEN = 15,
    LEMON = 16,
    HERBS = 17,
};

constexpr unsigned int CAPACITY = 24;  // room for more ingredients

const std::vector<std::string> INGREDIENT_NAMES = {
    "flour",   "sugar",     "butter", "egg",   "milk",         "yeast",  "salt",    "baking_powder", "cocoa",
    "vanilla", "olive_oil", "garlic", "pasta", "tomato_sauce", "cheese", "chicken", "lemon",         "herbs",
};

struct Recipe {
    unsigned int id;
    std::string name;
    BinarySet ingredients;
};

void print_ingredients(const BinarySet& bs) {
    bool first = true;
    for (unsigned int i : bs) {
        if (!first) std::cout << ", ";
        std::cout << (i < INGREDIENT_NAMES.size() ? INGREDIENT_NAMES[i] : std::to_string(i));
        first = false;
    }
}

void run() {
    section("Scenario 2 — Recipe matcher");

    // -----------------------------------------------------------------------
    // Build the recipe catalogue
    // -----------------------------------------------------------------------
    std::vector<Recipe> catalogue;

    auto make_recipe = [&](unsigned int id, const std::string& name, std::initializer_list<unsigned int> ings) {
        BinarySet bs(CAPACITY);
        for (auto i : ings) bs.add(i);
        catalogue.push_back({id, name, std::move(bs)});
    };

    make_recipe(1, "Chocolate cake", {FLOUR, SUGAR, BUTTER, EGG, MILK, COCOA, BAKING_POWDER, VANILLA});
    make_recipe(2, "Bread", {FLOUR, YEAST, SALT, MILK, BUTTER});
    make_recipe(3, "Pasta al pomodoro", {PASTA, TOMATO_SAUCE, GARLIC, OLIVE_OIL, SALT});
    make_recipe(4, "Cacio e pepe", {PASTA, CHEESE, SALT});
    make_recipe(5, "Scrambled eggs", {EGG, BUTTER, SALT, MILK});
    make_recipe(6, "Garlic bread", {FLOUR, YEAST, BUTTER, GARLIC, SALT});
    make_recipe(7, "Lemon chicken", {CHICKEN, LEMON, GARLIC, OLIVE_OIL, HERBS, SALT});
    make_recipe(8, "Pancakes", {FLOUR, EGG, MILK, SUGAR, BUTTER, BAKING_POWDER, SALT});
    make_recipe(9, "Simple omelette", {EGG, SALT, BUTTER});
    make_recipe(10, "Tomato pasta bake", {PASTA, TOMATO_SAUCE, CHEESE, GARLIC, OLIVE_OIL, SALT});

    // -----------------------------------------------------------------------
    // Index all recipes in the searcher
    // -----------------------------------------------------------------------
    BSSearcher searcher(CAPACITY);
    for (const auto& r : catalogue) searcher.add(r.id, r.ingredients);

    // Map id -> Recipe for display after the search.
    std::unordered_map<unsigned int, const Recipe*> by_id;
    for (const auto& r : catalogue) by_id[r.id] = &r;

    subsection("Full recipe catalogue");
    for (const auto& r : catalogue) {
        std::cout << "  [" << std::setw(2) << r.id << "] " << std::left << std::setw(22) << r.name << "  needs: ";
        print_ingredients(r.ingredients);
        std::cout << "\n";
    }

    // -----------------------------------------------------------------------
    // Pantry 1: well-stocked kitchen
    // -----------------------------------------------------------------------
    subsection("Pantry 1 — well-stocked kitchen");
    BinarySet pantry1(CAPACITY);
    for (unsigned int i :
         {FLOUR, SUGAR, BUTTER, EGG, MILK, YEAST, SALT, BAKING_POWDER, COCOA, VANILLA, OLIVE_OIL, GARLIC, PASTA, TOMATO_SAUCE, CHEESE})
        pantry1.add(i);

    std::cout << "  Available: ";
    print_ingredients(pantry1);
    std::cout << "\n\n  Recipes you can make:\n";

    auto ids1 = searcher.find_subsets(pantry1);
    std::sort(ids1.begin(), ids1.end());
    for (unsigned int id : ids1) std::cout << "    - " << by_id[id]->name << "\n";

    // -----------------------------------------------------------------------
    // Pantry 2: nearly empty fridge
    // -----------------------------------------------------------------------
    subsection("Pantry 2 — nearly empty fridge");
    BinarySet pantry2(CAPACITY);
    pantry2.add(EGG);
    pantry2.add(BUTTER);
    pantry2.add(SALT);

    std::cout << "  Available: ";
    print_ingredients(pantry2);
    std::cout << "\n\n  Recipes you can make:\n";

    auto ids2 = searcher.find_subsets(pantry2);
    std::sort(ids2.begin(), ids2.end());
    if (ids2.empty())
        std::cout << "    (none)\n";
    else
        for (unsigned int id : ids2) std::cout << "    - " << by_id[id]->name << "\n";

    // -----------------------------------------------------------------------
    // What single ingredient would unlock the most new recipes from pantry2?
    // Brute-force over all ingredients not yet in pantry2.
    // -----------------------------------------------------------------------
    subsection("Which single ingredient unlocks the most new recipes from pantry2?");

    unsigned int best_ingredient = 0;
    int best_gain = -1;

    for (unsigned int ing = 0; ing < CAPACITY; ++ing) {
        if (pantry2.contains(ing)) continue;

        BinarySet extended = pantry2;
        extended.add(ing);
        int gain = static_cast<int>(searcher.find_subsets(extended).size()) - static_cast<int>(ids2.size());
        if (gain > best_gain) {
            best_gain = gain;
            best_ingredient = ing;
        }
    }

    std::cout << "  Buy " << (best_ingredient < INGREDIENT_NAMES.size() ? INGREDIENT_NAMES[best_ingredient] : std::to_string(best_ingredient))
              << " to unlock " << best_gain << " more recipe(s).\n";

    // -----------------------------------------------------------------------
    // Remove a recipe from the index (e.g. seasonal menu change)
    // -----------------------------------------------------------------------
    subsection("Removing 'Lemon chicken' from the menu");
    searcher.remove(7, by_id[7]->ingredients);

    auto ids_after = searcher.find_subsets(BinarySet(CAPACITY, true));
    std::cout << "  Recipes still indexed: " << ids_after.size() << "\n";
}

}  // namespace recipes

// ============================================================================
//  Scenario 3: Course prerequisite validator
// ============================================================================
//
// Each course in the catalogue is identified by an index into the BinarySet.
// Some courses have prerequisite sets.
// Given the set of courses a student has already completed, we use
// BSSearcher to find all courses whose prerequisites are satisfied.

namespace university {

constexpr unsigned int CAPACITY = 32;  // up to 32 courses in the department

// Courses
enum : unsigned int {
    // Year 1
    CALC1 = 0,
    CALC2 = 1,
    LINEAR_ALG = 2,
    PROG1 = 3,
    PROG2 = 4,
    LOGIC = 5,
    // Year 2
    ALGO = 6,
    DATA_STRUCT = 7,
    PROB_STAT = 8,
    OS = 9,
    NETWORKS = 10,
    DATABASES = 11,
    // Year 3
    MACHINE_L = 12,
    COMPILERS = 13,
    DIST_SYS = 14,
    SECURITY = 15,
    GRAPH_THEORY = 16,
    // Year 4 / advanced
    DEEP_LEARN = 17,
    CRYPTO = 18,
    CLOUD = 19,
};

const std::vector<std::string> COURSE_NAMES = {
    "Calculus I",       "Calculus II",     "Linear Algebra",           "Programming I",     "Programming II", "Logic",
    "Algorithms",       "Data Structures", "Probability & Statistics", "Operating Systems", "Networks",       "Databases",
    "Machine Learning", "Compilers",       "Distributed Systems",      "Security",          "Graph Theory",   "Deep Learning",
    "Cryptography",     "Cloud",
};

struct Course {
    unsigned int id;
    std::string name;
    BinarySet prerequisites;  // empty = no prerequisites
};

void run() {
    section("Scenario 3 — Course prerequisite validator");

    // -----------------------------------------------------------------------
    // Define the course catalogue with prerequisites
    // -----------------------------------------------------------------------
    auto make_course = [&](unsigned int id, std::initializer_list<unsigned int> prereqs) -> Course {
        BinarySet bs(CAPACITY);
        for (auto p : prereqs) bs.add(p);
        return {id, COURSE_NAMES[id], std::move(bs)};
    };

    std::vector<Course> catalogue = {
        // Year 1 — no prerequisites
        make_course(CALC1, {}),
        make_course(CALC2, {CALC1}),
        make_course(LINEAR_ALG, {CALC1}),
        make_course(PROG1, {}),
        make_course(PROG2, {PROG1}),
        make_course(LOGIC, {}),
        // Year 2
        make_course(ALGO, {PROG2, DATA_STRUCT}),
        make_course(DATA_STRUCT, {PROG2}),
        make_course(PROB_STAT, {CALC2}),
        make_course(OS, {PROG2, DATA_STRUCT}),
        make_course(NETWORKS, {OS}),
        make_course(DATABASES, {PROG2, DATA_STRUCT}),
        // Year 3
        make_course(MACHINE_L, {PROB_STAT, LINEAR_ALG, PROG2}),
        make_course(COMPILERS, {ALGO, DATA_STRUCT}),
        make_course(DIST_SYS, {OS, NETWORKS, DATABASES}),
        make_course(SECURITY, {NETWORKS, CRYPTO}),
        make_course(GRAPH_THEORY, {ALGO, LINEAR_ALG}),
        // Advanced
        make_course(DEEP_LEARN, {MACHINE_L, LINEAR_ALG}),
        make_course(CRYPTO, {LOGIC, LINEAR_ALG, PROB_STAT}),
        make_course(CLOUD, {DIST_SYS}),
    };

    // -----------------------------------------------------------------------
    // Index every course by its prerequisite set.
    // The searcher answers: "which prerequisite sets are subsets of
    // the student's completed-course set?" — i.e. which courses
    // has the student satisfied all prerequisites for.
    // -----------------------------------------------------------------------
    BSSearcher searcher(CAPACITY);
    std::unordered_map<unsigned int, const Course*> by_id;

    for (const auto& c : catalogue) {
        searcher.add(c.id, c.prerequisites);
        by_id[c.id] = &c;
    }

    subsection("Full catalogue with prerequisites");
    for (const auto& c : catalogue) {
        std::cout << "  " << std::left << std::setw(26) << c.name << "  prereqs: ";
        if (c.prerequisites.empty()) {
            std::cout << "(none)";
        } else {
            bool first = true;
            for (unsigned int p : c.prerequisites) {
                if (!first) std::cout << ", ";
                std::cout << COURSE_NAMES[p];
                first = false;
            }
        }
        std::cout << "\n";
    }

    // -----------------------------------------------------------------------
    // Student A — first-year student, completed only Year-1 basics
    // -----------------------------------------------------------------------
    subsection("Student A — completed: Calc I, Prog I, Logic");
    BinarySet studentA(CAPACITY);
    studentA.add(CALC1);
    studentA.add(PROG1);
    studentA.add(LOGIC);

    // Find all courses whose prerequisites are satisfied.
    // We must exclude courses the student already completed.
    auto eligible = searcher.find_subsets(studentA);
    std::sort(eligible.begin(), eligible.end());

    std::cout << "  Eligible to enrol in:\n";
    for (unsigned int id : eligible) {
        if (!studentA.contains(id))  // don't re-suggest completed courses
            std::cout << "    - " << COURSE_NAMES[id] << "\n";
    }

    // -----------------------------------------------------------------------
    // Student B — completed most of the degree
    // -----------------------------------------------------------------------
    subsection("Student B — completed most courses");
    BinarySet studentB(CAPACITY);
    for (unsigned int c :
         {CALC1, CALC2, LINEAR_ALG, PROG1, PROG2, LOGIC, ALGO, DATA_STRUCT, PROB_STAT, OS, NETWORKS, DATABASES, MACHINE_L, COMPILERS, CRYPTO})
        studentB.add(c);

    std::cout << "  Completed " << studentB.size() << " courses.\n";

    auto eligibleB = searcher.find_subsets(studentB);
    std::sort(eligibleB.begin(), eligibleB.end());

    std::cout << "  Still available to take:\n";
    for (unsigned int id : eligibleB)
        if (!studentB.contains(id)) std::cout << "    - " << COURSE_NAMES[id] << "\n";

    // -----------------------------------------------------------------------
    // Which single course would unlock the most new options for Student A?
    // -----------------------------------------------------------------------
    subsection("Which course should Student A take next for maximum unlock?");

    // Courses Student A hasn't taken but is eligible for right now.
    std::vector<unsigned int> takeable;
    for (unsigned int id : eligible)
        if (!studentA.contains(id)) takeable.push_back(id);

    // Build the set of already-eligible course IDs for fast membership test.
    BinarySet enrolled_or_eligible(CAPACITY);
    for (unsigned int id : eligible) enrolled_or_eligible.add(id);

    unsigned int best_course = 0;
    int best_new = -1;

    for (unsigned int take : takeable) {
        BinarySet after = studentA;
        after.add(take);
        auto newly = searcher.find_subsets(after);
        // Count courses newly eligible that weren't eligible before
        // and that the student hasn't already taken.
        int new_eligible = 0;
        for (unsigned int id : newly)
            if (!after.contains(id) && !enrolled_or_eligible.contains(id)) ++new_eligible;

        if (new_eligible > best_new) {
            best_new = new_eligible;
            best_course = take;
        }
    }

    std::cout << "  Recommended: " << COURSE_NAMES[best_course] << " (unlocks " << best_new << " additional course(s))\n";

    // -----------------------------------------------------------------------
    // Show courses Student A cannot yet take and what is blocking them
    // -----------------------------------------------------------------------
    subsection("Courses Student A cannot yet enrol in (and what's missing)");

    for (const auto& c : catalogue) {
        if (enrolled_or_eligible.contains(c.id)) continue;
        BinarySet missing = c.prerequisites - studentA;
        std::cout << "  " << std::left << std::setw(26) << c.name << "  missing: ";
        bool first = true;
        for (unsigned int p : missing) {
            if (!first) std::cout << ", ";
            std::cout << COURSE_NAMES[p];
            first = false;
        }
        std::cout << "\n";
    }
}

}  // namespace university

// ============================================================================
//  Entry point
// ============================================================================

int main() {
    permissions::run();
    recipes::run();
    university::run();

    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  All scenarios completed.\n";
    std::cout << std::string(60, '=') << "\n\n";
    return 0;
}