#include <stack>
#include <string>
#include <stdexcept>

/**
 * Traverses the postfix expression exactly once and evaluates it using a stack.
 *
 * Each character is processed once, and every stack push/pop is O(1).
 *
 * Best case:    Θ(n)
 * Average case: Θ(n)
 * Worst case:   Θ(n)
 *
 * In every case, all n characters must be examined, and each character causes
 * only a constant amount of work.
 *
 * @param s correct postfix expression containing +, -, *, /
 *          and non-zero single-digit natural operands
 * @return the value of the expression
 */
double foo(const std::string& s) {
    std::stack<double> stack;

    for (char c : s) {
        if ('1' <= c && c <= '9') {
            stack.push(c - '0');
        } else {
            const double right = stack.top();
            stack.pop();

            const double left = stack.top();
            stack.pop();

            switch (c) {
                case '+':
                    stack.push(left + right);
                    break;
                case '-':
                    stack.push(left - right);
                    break;
                case '/':
                    stack.push(left / right);
                    break;
                case '*':
                    stack.push(left * right);
                    break;
                default:
                    throw std::invalid_argument("invalid character");
            }
        }
    }

    return stack.top();
}