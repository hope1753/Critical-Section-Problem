#include <stdio.h>

typedef struct {
    int x;
    int y;
} Point;
Point create_point(int x, int y) {
    Point p;
    p.x = x;
    p.y = y;
    return p;
}

int return_add_point(Point p1) {
    return p1.x + p1.y;
}
int main() {
    Point p = create_point(1, 2);  
    printf("%d\n", return_add_point(p));
    return 0;
    
}