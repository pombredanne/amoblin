#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>

#include "nn.h"

int usage(char *argv[])
{
    printf("usage: %s [matrix file] [weight file]\n", argv[0]);
    return 0;
}

int train_bp(Matrix* w1, Matrix* w2, Matrix* in, Matrix* out, FILE* vector_p, FILE* fp) {
    /* 初始化矩阵 */
    Matrix* n1;
    matrix_init(in->m, w1->n, &n1);

    Matrix* a1;
    matrix_init(n1->m, n1->n, &a1);

    Matrix* n2;
    matrix_init(a1->m, w2->n, &n2);

    Matrix* a2;
    matrix_init(n2->m, n2->n, &a2);

    Matrix* diff2;
    matrix_init(a2->m, a2->n, &diff2);

    Matrix* h2;
    matrix_init(a2->m, a2->n, &h2);

    Matrix* s2;
    matrix_init(a2->m, a2->n, &s2);


    Matrix* diff1;
    matrix_init(w2->n, a2->n, &diff1);

    Matrix* h1;
    matrix_init(a1->m, a1->n, &h1);

    Matrix* s1;
    matrix_init(a1->m, a1->n, &s1);

    Matrix* delta1;
    matrix_init(w1->m, w1->n, &delta1);

    Matrix* delta2;
    matrix_init(w2->m, w2->n, &delta2);

    /* 计时器 */
    int time_s = time((time_t*)NULL);

    /* 学习率 */
    double alpha = LEARN_RATE;

    double old_e = 9999;
    double e = PRECISION + 1;
    int n;
    printf("LOOP_MAX: %d\n", LOOP_MAX);
    for (n = 0; e > PRECISION && n < LOOP_MAX; n++) {
        e = 0;

        /* 输入正传 */
        matrix_dot_multiply(in, w1, n1, NORMAL);
        matrix_fnet(n1, a1);

        matrix_dot_multiply(a1, w2, n2, NORMAL);
        matrix_fnet(n2, a2);

        /* 误差反传 */
        matrix_plus(out, a2, diff2, -1);
        matrix_multiply(h2, diff2, s2);

        matrix_dot_multiply(w2, s2, diff1, REVERSE1);
        matrix_multiply(h1, diff1, s1);

        /* 更新权值 */
        matrix_dot_multiply(s1, in, delta1, REVERSE2);
        matrix_plus(w1, delta1, w1, alpha * -1);

        matrix_dot_multiply(s2, a1, delta2, REVERSE2);
        matrix_plus(w2, delta2, w2, alpha * -1);

        /* 计算输出误差 */
        matrix_fanshu(a2, out, &e);

        d_printf(1, "e:%f\n",e);
        //d_printf(1, "old e:%f\n",old_e);
        //double temp_e = e - old_e;
        //d_printf(1, "%f\n", temp_e);
        if (e > old_e) {
            d_printf(3, "e:%f\n",e);
            d_printf(3, "old_e:%f\n",old_e);
        }
        if (e <= old_e+0.5) {
            /* 进行权值更新 */
            old_e = e;
        } else {
            /* 取消权值更新 */
            alpha = 0.99 * alpha;
            d_printf(3, "alpha changed:%f\n", alpha);
        }

        if (0 == (n % LOG_DEN)) {
            /* 写入日志 */
            time_t time_e = time(NULL);
            struct tm *timeinfo;
            timeinfo = localtime(&time_e);
            int seconds = (int)difftime(time_e, time_s);
            int mins = seconds % 3600;
            int secs = mins % 60;
            syslog(LOG_USER|LOG_DEBUG, "%02d:%02d:%02d %02dh%02dm%02ds %d %f %f\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, seconds/3600, mins/60, secs, n, alpha, e);
            /* 保存权值矩阵 */
            fseek(vector_p, 0, SEEK_SET);
            fwrite(w1->matrix, HIDDEN_NODES * sizeof(double), IN_NODES, vector_p);
            fwrite(w2->matrix, OUT_NODES * sizeof(double), HIDDEN_NODES, vector_p);
            fflush(vector_p);
        }

        /* 保存图像数据 */
        if( n % PLOT_DEN== 0) {
            fprintf(fp, "%d %f %f\n", n/PLOT_DEN, e, alpha);
        }
    }
    /* 释放矩阵内存 */
    matrix_free(n1);
    matrix_free(a1);
    matrix_free(n2);
    matrix_free(a2);
    matrix_free(diff2);
    matrix_free(h2);
    matrix_free(s2);
    matrix_free(diff1);
    matrix_free(h1);
    matrix_free(s1);
    matrix_free(delta1);
    matrix_free(delta2);
    return 0;
}

int main(int argc, char* argv[])
{
    char in_file[128];
    char out_file[128];
    char xy_file[128];
    /* 记录日志 */
    int logfd = open( "nn.log", O_RDWR | O_CREAT | O_APPEND, 0644 );
    close(STDERR_FILENO);
    dup2(logfd, STDERR_FILENO);
    close(logfd);
    openlog(NULL, LOG_PERROR, LOG_DAEMON);

    /* 输入参数分析 */
    int ch;
    int flag=0;
    do {
        ch = getopt(argc, argv, "i:o:g:h");
        switch(ch) {
            case 'i':
                strncpy(in_file, optarg, sizeof(in_file) -1 );
                flag++;
                break;
            case 'o':
                strncpy(out_file, optarg, sizeof(out_file) -1 );
                flag++;
                break;
            case 'g':
                strncpy(xy_file, optarg, sizeof(xy_file) -1 );
                flag++;
                break;
            case 'h':
                usage(argv);
                break;
            case '?':
            default:
                break;
        }
    } while( -1 != ch);
    if(3 > flag) {
        usage(argv);
        return 0;
    }

    /* 读取样本数据 */
    FILE *vector_p = NULL;
    vector_p = fopen(in_file, "rb");
    if (vector_p == NULL) {
        printf("Error! File %s does not exist.\n", in_file);
        exit(0);
    }
    /* 获取数据量 */
    int data_size;
    fread(&data_size, sizeof(int), 1, vector_p);
    /* 输入输出矩阵分配内存 */
    Matrix *in;
    matrix_init(data_size, IN_NODES, &in);
    Matrix *out;
    matrix_init(data_size, OUT_NODES, &out);

    /* 读取输入输出矩阵 */
    unsigned char **tmp_in = (unsigned char**) malloc(sizeof(unsigned char*) * data_size);
    int i,j;
    for(i=0;i<data_size;i++) {
        tmp_in[i] = NULL;
        tmp_in[i] = (unsigned char*) malloc(sizeof(unsigned char) * IN_NODES);
        if(NULL == tmp_in[i]) {
            printf("malloc failed.\n");
            exit(0);
        }
        fread(tmp_in[i], sizeof(unsigned char), IN_NODES, vector_p);
        fread(out->matrix[i], sizeof(double), OUT_NODES, vector_p);
    }
    fclose(vector_p);

    matrix_copy(tmp_in, in);

   /* 初始化隐含层权值矩阵 */
    Matrix *w1;
    matrix_init(IN_NODES, HIDDEN_NODES, &w1);

   /* 初始化输出层权值矩阵 */
    Matrix *w2;
    matrix_init(HIDDEN_NODES, OUT_NODES, &w2);

    vector_p = NULL;
    vector_p = fopen(out_file, "rb+");
    if (NULL == vector_p) { //不存在，使用随机数初始化
        /* 初始化权值矩阵 */
        syslog(LOG_INFO, "初始化矩阵");
        matrix_set_random(w1);
        matrix_set_random(w2);
        vector_p = fopen(out_file, "wb");
    } else {    //使用上一次的矩阵
        syslog(LOG_INFO, "使用上次矩阵");
        for(i= 0; i< w1->m; i++) {
            fread(w1->matrix[i], sizeof(double), HIDDEN_NODES, vector_p);
        }
        for(i= 0; i< w2->m; i++) {
            fread(w2->matrix[i], sizeof(double), OUT_NODES, vector_p);
        }
    }
    printf("%f\n", w1->matrix[0][0]);

    /* 保存数据点 */
    FILE *fp = NULL;
    fp = fopen(xy_file, "w");
    if (NULL == fp) {
        syslog(LOG_ERR, "创建坐标数据文件失败");
        exit(0);
    }

    /* 训练 */
    printf("开始网络训练\n");
    train_bp(w1, w2, in, out, vector_p, fp);             //训练bp神经网络
    fclose(vector_p);
    fclose(fp);

    /* 释放内存 */
    matrix_free(in);
    matrix_free(out);
    matrix_free(w1);
    matrix_free(w2);
    closelog();
    return 0;
}
