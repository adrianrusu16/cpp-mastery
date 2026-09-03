#include <iostream>
#include <memory>
#include <sstream>
#include <vector>
#include <stdexcept>

class Token {
public:
    virtual ~Token() = default;

    virtual std::string toString() = 0;
};

class Constant : public Token {
    int value;

public:
    explicit Constant(const int v) : value(v) {
    }

    [[nodiscard]] std::string toString() override { return std::to_string(value); }
};

class BinaryOperator : public Token {
    char operatorr;

public:
    explicit BinaryOperator(const char o) : operatorr(o) {
        if (operatorr != '+' && operatorr != '-' && operatorr != '*' && operatorr != '/')
            throw std::logic_error("operatorr not valid");
    }

    [[nodiscard]] std::string toString() override { return { operatorr }; }
};

class ExpressionTree {
    std::unique_ptr<Token> t;
    std::unique_ptr<ExpressionTree> left;
    std::unique_ptr<ExpressionTree> right;

public:
    explicit ExpressionTree(const std::string &s) {
        // Implementation not required by the exercise.
        // Assume the correct expression tree is constructed.
    };

    [[nodiscard]] Token* getToken() const { return t.get(); }
    [[nodiscard]] ExpressionTree *getLeft() const { return left.get(); }
    [[nodiscard]] ExpressionTree *getRight() const { return right.get(); }

    [[nodiscard]] bool isLeaf() const { return left == nullptr && right == nullptr; }

    ~ExpressionTree() = default;
};

class Homework {
    std::vector<std::unique_ptr<ExpressionTree>> expressions;

public:
    Homework() = default;

    void addExpression(const std::string &s) {
        expressions.push_back(std::make_unique<ExpressionTree>(s));
    }

    std::vector<std::unique_ptr<ExpressionTree>>& getExpressions() { return expressions; }

    ~Homework() = default;
};

static void postfixTraversal(const ExpressionTree *tree, std::stringstream &ss) {
    if (tree->isLeaf()) {
        ss << tree->getToken()->toString();
        return;
    }

    postfixTraversal(tree->getLeft(), ss);
    postfixTraversal(tree->getRight(), ss);

    ss << tree->getToken()->toString();
}

static std::string toString(const ExpressionTree *tree) {
    std::stringstream ss;

    postfixTraversal(tree, ss);

    return ss.str();
}

static void foo() {

    Homework h;

    h.addExpression("(5-3)+(2*6)");
    h.addExpression("5-(3*4+5)/(6-2)");
    h.addExpression("3*(5/(3+2)-4)+6");

    for (auto const& tree : h.getExpressions()) {
        std::cout << toString(tree.get()) << '\n';
    }
}
