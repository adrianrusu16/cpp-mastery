// Intentionally does not compile.
// Build this file manually when studying the diagnostic difference between
// constrained and unconstrained templates.

struct Nothing {
};

template<typename T>
T unconstrained_add(T a, T b)
{
    return a + b;
}

int main()
{
    Nothing a;
    Nothing b;
    (void)unconstrained_add(a, b);
}
