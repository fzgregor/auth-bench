library(ggplot2)
library(plyr)

args <- commandArgs(trailingOnly=TRUE)

df <- read.table(args[1], header=T,sep=",")

pl <- ggplot(df, aes(x=record_len, y=throughput, color=threads, group=threads)) +
    geom_point() +
    geom_line() + 
    theme_bw() +
    theme(legend.title=element_blank(), legend.position="top") +
    scale_x_log10() + # scale_y_log10() +
    facet_grid(name ~ .)
ggsave("authentication_performance.pdf", width=8, height=8, limitsize=FALSE)
