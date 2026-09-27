#include <iostream>
#include <stdexcept>

class Sofa {
    Sofa *next = nullptr;
    Sofa *previous = nullptr;
    double width;
    double height;

public:
    Sofa(const double w, const double h) : width(w), height(h) {
        if (w <= 0 || h <= 0) {
            throw std::invalid_argument("Width and height must be greater than 0.");
        }
    };

    virtual double getCoefficient() = 0;

    virtual double getPrice() {
        return (width + height) * getCoefficient();
    };

    [[nodiscard]] Sofa *getNext() const { return next; }
    void setNext(Sofa *sofa) { this->next = sofa; }
    [[nodiscard]] Sofa *getPrevious() const { return previous; }
    void setPrevious(Sofa *sofa) { this->previous = sofa; }

    virtual ~Sofa() = default;
};

class ClassicSofa : public Sofa {
public:
    ClassicSofa(const double w, const double h) : Sofa(w, h) {
    };

    double getCoefficient() override { return 1.5; }
};

class SofaWithRoundArms : public Sofa {
    const double meterPrice = 10;
    double lengthOfArms;

public:
    SofaWithRoundArms(const double w, const double h, const double loa) : Sofa(w, h), lengthOfArms(loa) {
        if (loa <= 0) {
            throw std::invalid_argument("lengthOfArms must be greater than 0.");
        }
    };

    double getCoefficient() override { return 2; }
    double getPrice() override { return Sofa::getPrice() + meterPrice * lengthOfArms; }
};

class SellingProducts {
    Sofa *first = nullptr;
    Sofa *last = nullptr;

public:
    SellingProducts() = default;

    /**
     * Parses the linked list of Sofas and adds it such that the descending price order will be maintained.
     *
     * Best case:    O(1) - The price is the highest in the list and is added at the beginning.
     * Average case: Θ(n) - let n be the number of nodes to be traversed to find the place for the new sofa, then the
     * average number of nodes to be traversed is (0+1+2+...+n) / (n+1) = n*(n+1) / 2 / (n+1) = O(n/2) = Θ(n)
     * Worst case:   Θ(n) - the price is the lowest in the list and the entire list needs to be traversed.
     *
     * @param s Sofa to be added
     */
    void addProductsToSell(Sofa *s) {
        if (s == nullptr) {
            throw std::invalid_argument("Null pointer provided.");
        }
        if (first == nullptr) {
            first = s;
            last = s;
            s->setNext(nullptr);
            s->setPrevious(nullptr);
            return;
        }
        if (s->getPrice() >= first->getPrice()) {
            s->setNext(first);
            s->setPrevious(nullptr);
            first->setPrevious(s);
            first = s;
            return;
        }

        Sofa *current = first;

        while (current->getNext() != nullptr && current->getNext()->getPrice() > s->getPrice()) {
            current = current->getNext();
        }

        Sofa *nextBlock = current->getNext();
        current->setNext(s);
        s->setPrevious(current);
        s->setNext(nextBlock);

        if (nextBlock == nullptr) {
            last = s;
        } else {
            nextBlock->setPrevious(s);
        }
    }

    [[nodiscard]] Sofa *getFirstProduct() const { return first; }
    void setFirstProduct(Sofa *s) { first = s; }
    [[nodiscard]] Sofa *getLastProduct() const { return last; }
    void setLastProduct(Sofa *s) { last = s; }

    ~SellingProducts() {
        const Sofa *current = first;
        while (current != nullptr) {
            const Sofa *next = current->getNext();
            delete current;
            current = next;
        }
    }
};

/**
 * Traverses the sofas in ascending order and displays their prices.
 *
 * Best case:    Θ(n)
 * Average case: Θ(n)
 * Worst case:   Θ(n)
 *
 * The list behaves like a double ended linked list. To display the entire list, all n nodes must be visited.
 *
 * @param s the Selling product that contains sofas.
 */
void displaySofasAscending(const SellingProducts *s) {
    Sofa *current = s->getLastProduct();
    while (current != nullptr) {
        std::cout << current->getPrice() << '\n';
        current = current->getPrevious();
    }
}

void removeSofasFromRange(SellingProducts *s, const double startPrice, const double endPrice) {
    if (s == nullptr || startPrice > endPrice || s->getFirstProduct() == nullptr) return;

    Sofa *current = s->getFirstProduct();
    while (current != nullptr) {
        Sofa *nextProduct = current->getNext();

        if (current->getPrice() >= startPrice && current->getPrice() <= endPrice) {
            if (current->getPrevious() != nullptr) {
                current->getPrevious()->setNext(current->getNext());
            } else {
                s->setFirstProduct(current->getNext());
            }

            if (current->getNext() != nullptr) {
                current->getNext()->setPrevious(current->getPrevious());
            } else {
                s->setLastProduct(current->getPrevious());
            }

            delete current;
        }
        current = nextProduct;
    }
}

void addSofas(SellingProducts *s) {
    s->addProductsToSell(new SofaWithRoundArms(10, 20, 1.2));
    s->addProductsToSell(new SofaWithRoundArms(11, 20, 0.9));
    s->addProductsToSell(new ClassicSofa(20, 30));
    s->addProductsToSell(new ClassicSofa(22, 30));

    displaySofasAscending(s);

    std::cout << "\nRemove sofas from range 71 -> 76\n\n";

    removeSofasFromRange(s, 71, 76);

    displaySofasAscending(s);
}


int main() {
    SellingProducts s;
    addSofas(&s);

    return 0;
}
