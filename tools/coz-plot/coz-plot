#!/usr/bin/env Rscript

require(ggplot2)
require(plyr)

args <- commandArgs(trailingOnly = TRUE)

filename <- 'stdin'
if(length(args) == 1) {
  filename <- args[1]
}

# Read the CSV file
dat <- read.csv(filename, sep='\t')

# Compute the number of points for each line
dat$points <- tabulate(dat$line)[dat$line]

# Prune out lines without a complete set of samples
dat <- subset(dat, points >= 10)

# Remove points above 1.0 or below -1.0
dat <- subset(dat, counter_speedup < 1.0)
dat <- subset(dat, counter_speedup > -1.0)

# Compute the slope of each line's regression line
slopes <- daply(dat, .(line), function(x) {
  model <- lm(counter_speedup~line_speedup, data=x)
  return(coef(model)[2])
})

#maxes <- daply(dat, .(line), function(x) {
#  q <- quantile(x$counter_speedup, probs = c(0.9))
#  return(q[1])
#})

#mins <- daply(dat, .(line), function(x) {
#  q <- quantile(x$counter_speedup, probs = c(0.1))
#  return(q[1])
#})

#dat <- subset(dat, maxes[line] > 0.3 || mins[line] < 0.3)

# Save the initial line factor order
l <- levels(dat$line)

# Reorder the line factor by slope
dat$line <- factor(dat$line, levels=l[rev(order(slopes))], ordered=TRUE)

# Graph it
ggplot(dat, aes(x=line_speedup, y=counter_speedup, color=counter)) +
  geom_point(size=1.5) +
  facet_wrap(~line) +
  geom_smooth(method='lm', se=FALSE) +
  theme(legend.position='bottom') +
  scale_y_continuous(limits=c(-1, 1))
