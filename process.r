# Run with r --no-save --args [results filename here] < process.r

require(ggplot2)
require(plyr)

args <- commandArgs(trailingOnly = TRUE)

# Read the CSV file
dat <- read.csv(args[1], sep='\t')

# Compute the slope of each line's regression line
slopes <- daply(dat, .(line), function(x) {
  model <- lm(counter_speedup~line_speedup, data=x, weights=samples)
  return(coef(model)[2])
})

# Save the initial line factor order
l <- levels(dat$line)

# Reorder the line factor by slope
dat$line <- factor(dat$line, levels=l[rev(order(slopes))], ordered=TRUE)

# Compute the number of points for each line
dat$points <- tabulate(dat$line)[dat$line]

# Prune out lines with a single point
dat <- subset(dat, points > 3)

# Graph it
ggplot(dat, aes(x=line_speedup, y=counter_speedup, color=counter, weight=samples)) +
  geom_point() +
  facet_wrap(~line) +
  geom_smooth(method='lm', se=FALSE) +
  theme(legend.position='bottom') +
  scale_y_continuous(limits=c(-1, 1))