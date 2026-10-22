#!/usr/bin/env python3
# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


"""
Attempt to estimate what pieces of history can best predict workloads' branches.

First, read in a CSV file full of in-order branch instruction information
generated from is_branch_csv (i.e. entrance PC, branch PC, branch target,
taken/not-taken), creating branch history by copying and shifting branches'
data onto the subsequent rows at increasing offsets. Once each row in the
pandas DataFrame has its branch history, group the DataFrame based on entrance
PC, and train one decision tree per group. From the trained decision trees,
collect statistics about which branch information at what depths is
contributing most to predicting branches.
"""

import argparse
import math
from collections import defaultdict

import pandas as pd
from sklearn.model_selection import train_test_split
from sklearn.tree import DecisionTreeClassifier

# These are the columns to propagate back "in time" to construct both local and
# global branch history
history_columns = [
    "branch_pc",
    "is_direct",
    "is_conditional",
    "is_call",
    "is_return",
    "is_backwards_direct",
    "target",
    "taken",
    #    'last_load_va',
]
# The columns containing the information we want to predict
prediction_columns = [
    "branch_pc",
    "target",
    "taken",
]
# Don't even mess with these columns at all
drop_columns = [
    #    'last_load_va',
    "instructions",
    "branches",
    "taken_branches",
]
# These columns cannot be directly used for the inputs for predicting branches
# because they are "cheating" - they are either directly or indirectly part of
# the prediction
branch_types = [
    "is_direct",
    "is_conditional",
    "is_call",
    "is_return",
    "is_backwards_direct",
]
prediction_exclusion_columns = [
    "branch_pc",
    "target",
    "taken",
] + branch_types

# Create a mapping from the local and global history column names to their
# history length
global_history_feature_lengths = {}
local_history_feature_lengths = {}
feature_categories = {}
all_categories = set()


def add_feature_category(feature, category):
    if feature not in feature_categories:
        feature_categories[feature] = set()
    feature_categories[feature].add(category)
    all_categories.add(category)


def get_branch_df(csv_filename, global_history_depth, local_history_depth, include_untaken_history):
    print(f"Reading branch data from {csv_filename}, formatting it, and populating history...")

    # Read data in, ensuring it stays as an integer since that's what scikit
    # learn's DecisionTreeClassifier needs
    # read_csv_types = defaultdict(int)
    df = pd.read_csv(csv_filename)  # , dtype=read_csv_types)
    df = df.astype(int)

    # drop unwanted columns if they exist
    drop_column_intersection = df.columns.intersection(drop_columns)
    if len(drop_column_intersection) > 0:
        df.drop(labels=drop_column_intersection, axis=1, inplace=True)

    # Drop untaken branches before shifting history if requested
    if not include_untaken_history:
        df = df[df["taken"] == 1]

    # Generate global branch history by shifting rows down
    to_concat = []
    for depth in range(1, global_history_depth + 1):
        for orig_column in history_columns:
            history_name = f"{orig_column}_g{depth:03d}"
            to_concat.append(df[orig_column].shift(depth, fill_value=0).rename(history_name))
            global_history_feature_lengths[history_name] = depth
            add_feature_category(history_name, "global_history")
            add_feature_category(history_name, orig_column)
            add_feature_category(history_name, f"global_{orig_column}")
            add_feature_category(history_name, "last_seen_or_global_history")
            add_feature_category(history_name, "local_or_global_or_imli_or_last_seen")
            add_feature_category(history_name, "any")
            if orig_column in branch_types:
                add_feature_category(history_name, "global_branch_type")
                add_feature_category(history_name, "branch_type")

    # Generate local branch history by shifting rows down within each 'entry
    # point' group
    grouped_df = df.groupby("entry_point")
    for depth in range(1, local_history_depth + 1):
        for orig_column in history_columns:
            history_name = f"{orig_column}_l{depth:03d}"
            to_concat.append(grouped_df[orig_column].shift(depth, fill_value=0).rename(history_name))
            local_history_feature_lengths[history_name] = depth
            add_feature_category(history_name, "local_history")
            add_feature_category(history_name, orig_column)
            add_feature_category(history_name, f"local_{orig_column}")
            add_feature_category(history_name, "imli_or_local_history")
            add_feature_category(history_name, "imli_or_last_seen_or_local_history")
            add_feature_category(history_name, "local_or_global_or_imli_or_last_seen")
            add_feature_category(history_name, "any")
            if orig_column in branch_types:
                add_feature_category(history_name, "local_branch_type")
                add_feature_category(history_name, "branch_type")

    # Add non-local-history, non-global-history categories for columns
    add_feature_category("imli", "local_or_global_or_imli_or_last_seen")
    add_feature_category("imli", "imli_or_last_seen_or_local_history")
    add_feature_category("imli", "imli_or_local_history")
    add_feature_category("imli", "imli_or_last_seen")
    add_feature_category("imli", "imli_or_loop_tracker")
    add_feature_category("imli", "any")
    for i in range(16):
        add_feature_category(f"loop_tracker_{i}", "loop_tracker")
        add_feature_category(f"loop_tracker_{i}", "imli_or_loop_tracker")
        add_feature_category(f"loop_tracker_{i}", "any")

    for i in range(16):
        add_feature_category(f"last_seen_{i}", "last_seen")
        add_feature_category(f"last_seen_{i}", "imli_or_last_seen")
        add_feature_category(f"last_seen_{i}", "imli_or_last_seen_or_local_history")
        add_feature_category(f"last_seen_{i}", "last_seen_or_global_history")
        add_feature_category(f"last_seen_{i}", "local_or_global_or_imli_or_last_seen")
        add_feature_category(f"last_seen_{i}", "any")
    add_feature_category("return_stack", "any")

    add_feature_category("last_load_va", "last_load_va_any")
    if "last_load_va" in df.columns:
        df["last_load_va"] = df["last_load_va"].shift(1, fill_value=0)
        for i in range(0, 52, 1):
            load_va_name = f"last_load_va_bit{i}"
            to_concat.append((pd.to_numeric(df["last_load_va"]) & (2**i)).rename(load_va_name))
            add_feature_category(load_va_name, "any")
            add_feature_category(load_va_name, "last_load_va_any")

    df = pd.concat(to_concat + [df], axis=1)

    # Return only rows representing taken branches
    return df[df["taken"] == 1]


def split_x_y(df):
    x = df[df.columns.difference(prediction_exclusion_columns)]
    y = df[prediction_columns]
    return x, y


def train_tree(X_train, y_train, ccp_alpha=0.0):
    #    print(f'Training model on {len(df_train)} samples...')
    #    num_samples = df_train.shape[0]

    # Train the model based on the training data
    clf = DecisionTreeClassifier(
        criterion="log_loss",
        #        min_weight_fraction_leaf=min(0.5, 3.0/num_samples),
        #        min_samples_split=3,
        ccp_alpha=max(0.0, ccp_alpha),
        random_state=5,  # solid random number 5
    )
    clf = clf.fit(X_train, y_train)
    #    print(ccp_alphas)
    #    print(impurities)

    #    depth = clf.get_depth()
    #    leaves = clf.get_n_leaves()
    #    print(f'Trained decision tree with depth {depth} and {leaves} leaves.')
    #    unique_prediction_results = len(y_train.groupby(prediction_columns))
    #    print(f'There were a total of {unique_prediction_results} unique prediction results in the training sets')

    return clf


def report_accuracy_df(clf, X_test, y_test):
    #    print(f'Testing model on {len(df_test)} samples...')
    predictions_test = clf.predict(X_test)

    # Measure how accurate the test predictions were
    accurate_df = y_test == predictions_test
    accurate_df["overall"] = accurate_df.all(axis=1)

    #    test_predictions = accurate_df['all'].count()
    #    print(f'\nAccuracy of {test_predictions} test predictions:')
    #    print(accurate_df.sum() / accurate_df.count())

    return accurate_df.add_suffix("_accuracy")


def report_accuracy(clf, X_test, y_test):
    accurate_df = report_accuracy_df(clf, X_test, y_test)
    correct_predictions = accurate_df["overall_accuracy"].sum()
    total_predictions = accurate_df["overall_accuracy"].count()
    return correct_predictions * 1.0 / total_predictions


def print_prediction_path(clf, X_test):
    feature = clf.tree_.feature
    threshold = clf.tree_.threshold

    node_indicator = clf.decision_path(X_test)
    leaf_id = clf.apply(X_test)

    sample_id = 0
    # obtain ids of the nodes `sample_id` goes through, i.e., row `sample_id`
    node_index = node_indicator.indices[node_indicator.indptr[sample_id] : node_indicator.indptr[sample_id + 1]]

    print("Rules used to predict sample {id}:\n".format(id=sample_id))
    for node_id in node_index:
        # continue to the next node if it is a leaf node
        if leaf_id[sample_id] == node_id:
            continue

        # check if value of the split feature for sample 0 is below threshold
        if X_test.iloc[sample_id, feature[node_id]] <= threshold[node_id]:
            threshold_sign = "<="
        else:
            threshold_sign = ">"

        feature_name = X_test.columns[feature[node_id]]
        print(
            "decision node {node} : (X_test[{sample}, {feature}] = {value}) {inequality} {threshold})".format(
                node=node_id,
                sample=sample_id,
                feature=feature_name,
                value=X_test.iloc[sample_id, feature[node_id]],
                inequality=threshold_sign,
                threshold=threshold[node_id],
            )
        )


def get_useful_feature_counts(clf, X_test):
    # Calculate how frequently used a feature was
    path = clf.decision_path(X_test)
    path_df = pd.DataFrame(path.toarray())

    # The following should be true for all leaf nodes:
    #    children_left[node_id] == children_right[node_id]
    children_left = clf.tree_.children_left
    children_right = clf.tree_.children_right
    nodes_to_features = clf.tree_.feature

    def is_leaf_node(node_id):
        # I don't really understand why, but this is the test for a leaf node
        # in scikit-learn's decision trees
        return children_left[node_id] == children_right[node_id]

    # Traverse the tree's nodes, mapping each one to the "feature" (i.e. column
    # in the input set) it uses to make its decision. Create a dataframe
    # (feature_df) where each row is an individual test prediction and each
    # column is a count of the number of times a particular "feature" was used
    # to make that prediction.
    unique_feature_ids = set([nodes_to_features[n] for n in range(len(nodes_to_features)) if not is_leaf_node(n)])
    feature_df = pd.DataFrame(index=path_df.index, columns=list(unique_feature_ids)).fillna(0)
    seen_features = set()
    global_max_depths = pd.DataFrame(index=path_df.index)
    local_max_depths = pd.DataFrame(index=path_df.index)
    categories_df = pd.DataFrame(index=path_df.index)
    global_max_depths["max_global_depth"] = 0
    local_max_depths["max_local_depth"] = 0
    for node_id in range(len(nodes_to_features)):
        # detect if this node is a leaf node. If so, ignore it for our
        # calculations because only internal nodes make decisions
        if is_leaf_node(node_id):
            continue

        feature = nodes_to_features[node_id]
        assert feature >= 0

        assert feature in feature_df
        feature_df[feature] = feature_df[feature] + path_df[node_id]
        if feature not in seen_features:
            seen_features.add(feature)

    seen_categories = set()
    for feature in seen_features:
        feature_name = X_test.columns[feature]

        # If this feature was part of either global or local history,
        # calculate the max such history used
        if feature_name in global_history_feature_lengths:
            sel = (feature_df[feature] > 0) & (
                global_max_depths["max_global_depth"] < global_history_feature_lengths[feature_name]
            )
            global_max_depths["max_global_depth"][sel] = global_history_feature_lengths[feature_name]
        elif feature_name in local_history_feature_lengths:
            sel = (feature_df[feature] > 0) & (
                local_max_depths["max_local_depth"] < local_history_feature_lengths[feature_name]
            )
            local_max_depths["max_local_depth"][sel] = local_history_feature_lengths[feature_name]

        # Group counts into feature categories, counts once for each prediction
        # which used anything in a given category
        if feature_name in feature_categories:
            for category in feature_categories[feature_name]:
                if category not in seen_categories:
                    seen_categories.add(category)
                    categories_df[category] = False
                categories_df[feature_df[feature] > 0] = True

    # Get a histogram of the max used global and local history depths per entry
    global_max_depths = global_max_depths["max_global_depth"].value_counts().to_frame("counts")
    global_max_depths.index = global_max_depths.index.map(lambda depth: f"max_depth_g{depth:03d}")
    local_max_depths = local_max_depths["max_local_depth"].value_counts().to_frame("counts")
    local_max_depths.index = local_max_depths.index.map(lambda depth: f"max_depth_l{depth:03d}")

    feature_df = feature_df.apply(lambda x: x > 0)
    feature_df.rename(lambda col: X_test.columns[col], inplace=True, axis="columns")
    feature_usage_counts = pd.concat(
        [
            feature_df.sum().to_frame("counts"),
            categories_df.sum().to_frame("counts"),
            global_max_depths,
            local_max_depths,
        ],
        sort=True,
    )

    return feature_usage_counts["counts"].to_frame("counts").T


def get_metrics(clf, df_test):
    X_test, y_test = split_x_y(df_test)

    # Report the accuracy of the trained tree
    accurate_df = report_accuracy_df(clf, X_test, y_test)
    correct_predictions = accurate_df["overall_accuracy"].sum()
    total_predictions = accurate_df["overall_accuracy"].count()

    results = pd.DataFrame()

    def feature_counts(prev_results, correctness, count):
        predictions_selector = accurate_df["overall_accuracy"] == correctness
        feature_usage_counts = get_useful_feature_counts(clf, X_test[predictions_selector])
        feature_usage_counts["correct"] = correctness
        feature_usage_counts["entry_point"] = count
        to_join = accurate_df[predictions_selector].sum().to_frame("counts").T
        feature_usage_counts = feature_usage_counts.join(to_join)

        return pd.concat([prev_results, feature_usage_counts], sort=True, ignore_index=True)

    if correct_predictions > 0:
        results = feature_counts(results, True, correct_predictions)
    if correct_predictions < total_predictions:
        results = feature_counts(results, False, total_predictions - correct_predictions)

    return results.reset_index(drop=True)


def find_best_tree(df_train, df_test):
    """
    Find the best (well, our approximation of the best) decision tree for the
    given train/test data and return a tree fitted to the train data with those
    parameters.
    """
    # Split training and test dataframe into the question (X) and answer (y)
    X_train, y_train = split_x_y(df_train)
    X_test, y_test = split_x_y(df_test)

    # Train a tree with ccp_alpha=0.0 and query the CCP alphas from that tre
    initial_tree = train_tree(X_train, y_train, ccp_alpha=0.0)
    path = initial_tree.cost_complexity_pruning_path(X_train, y_train)
    ccp_alphas, impurities = path.ccp_alphas, path.impurities

    #    print(f'find_best_tree:')

    # Only calculate an accuracy once
    accuracies = [None for i in range(len(ccp_alphas))]

    def ccp_accuracy(idx):
        if accuracies[idx] is None:
            ccp_alpha = ccp_alphas[idx]
            clf = train_tree(X_train, y_train, ccp_alpha=ccp_alpha)
            accuracy = report_accuracy(clf, X_test, y_test)
            #            print(f'    calculated accuracy of idx={idx}, ccp_alpha={ccp_alpha}: {accuracy:.3f}')
            accuracies[idx] = accuracy
        return accuracies[idx]

    def sample_accuracies(
        prev_low_idx, prev_high_idx, min_alpha_granularity=0.0001, range_multiplier=1.0, num_samples=8
    ):
        prev_range = prev_high_idx - prev_low_idx
        low_idx = max(0, prev_low_idx - math.ceil(prev_range * range_multiplier))
        high_idx = min(len(accuracies) - 1, prev_high_idx + math.ceil(prev_range * range_multiplier))
        #        low_idx = prev_low_idx
        #        high_idx = prev_high_idx
        #        prev_range = ccp_alphas[prev_high_idx] - ccp_alphas[prev_low_idx]
        #
        #        # First, determine the indexes we will sample
        #        for i, ccp_alpha in enumerate(ccp_alphas):
        #            if i < low_idx and ccp_alpha > ccp_alphas[prev_low_idx] - prev_range * range_multiplier:
        #                low_idx = i
        #            if i > high_idx and ccp_alpha < ccp_alphas[prev_high_idx] + prev_range * range_multiplier:
        #                high_idx = i

        # sample the highest and lowest of our new range
        ccp_accuracy(low_idx)
        ccp_accuracy(high_idx)

        #        # sample every min_alpha_granularity alpha values within this range
        #        last_alpha = ccp_alphas[low_idx]
        #        for i, alpha in enumerate(ccp_alphas):
        #            if i < low_idx or i > high_idx:
        #                continue
        #            if alpha >= last_alpha + min_alpha_granularity:
        #                last_alpha = alpha
        #                ccp_accuracy(i)
        for i in range(low_idx, high_idx, max(1, math.floor((high_idx - low_idx) / num_samples))):
            ccp_accuracy(i)

    # Return the indexes (into accuracies) of the n most accurate samples we
    # have seen so far
    def best_accuracy_index_range(n=1):
        calculated_accuracies = [a for a in enumerate(accuracies) if a[1] is not None]
        calculated_accuracies = sorted(calculated_accuracies, key=lambda a: a[1], reverse=True)
        best_indexes = [a[0] for a in calculated_accuracies[: min(len(calculated_accuracies), n)]]
        return (min(best_indexes), max(best_indexes))

    # First, make sure we have sampled the highest and lowest ccp_alpha
    ccp_accuracy(0)
    ccp_accuracy(len(ccp_alphas) - 1)

    prev_best = None
    curr_accuracy_range = best_accuracy_index_range(n=2)
    same_best_in_a_row = 0

    MIN_ALPHA_GRANULARITY = 0.0001
    INITIAL_ALPHA_SAMPLES = 10
    min_alpha_granularity = (ccp_alphas[-1] - ccp_alphas[0]) / INITIAL_ALPHA_SAMPLES
    range_multiplier = 1.0
    num_samples = 8

    while True:
        #        print(f'  iter best={prev_best}, idx_range={curr_accuracy_range}')
        sample_accuracies(
            curr_accuracy_range[0],
            curr_accuracy_range[1],
            min_alpha_granularity,
            range_multiplier,
            num_samples=num_samples,
        )

        # Calculate the indices of the currently-sampled ccp_alphas with
        # accuracies in the top 4 samples
        curr_accuracy_range = best_accuracy_index_range(n=4)

        # If we have to seen the same 'best' thrice in a row, call it a success
        curr_best = accuracies[best_accuracy_index_range(n=1)[0]]
        if prev_best is not None and abs(curr_best - prev_best) < 0.001:
            same_best_in_a_row += 1
            num_samples = num_samples * 1.5
            range_multiplier = range_multiplier / 2
            if same_best_in_a_row == 3:
                break
        else:
            same_best_in_a_row = 0
            num_samples = 8
            range_multiplier = 1.0
            prev_best = curr_best

        min_alpha_granularity = min_alpha_granularity / 1.75

    #    # Try all alphas at least MIN_ALPHA_GRANULARITY apart
    #    last_alpha = None
    #    for i, alpha in enumerate(ccp_alphas):
    #        if not last_alpha or alpha >= last_alpha + MIN_ALPHA_GRANULARITY:
    #            last_alpha = alpha
    #            ccp_accuracy(i)

    #    print(f'\nCCP alphas: {len(ccp_alphas)}')
    #    best_accuracy = max([a for a in accuracies if a is not None])
    #    for i, ccp_alpha in enumerate(ccp_alphas):
    #        accuracy = ccp_accuracy(i)
    #        if accuracy > best_accuracy:
    #            best_accuracy = accuracy
    #        print(f'ccp_alpha {ccp_alpha}, accuracy: {accuracy:.3f}')

    assert best_accuracy_index_range(n=1)[0] == best_accuracy_index_range(n=1)[1]
    best_accuracy = accuracies[best_accuracy_index_range(n=1)[0]]
    for i, ccp_alpha in reversed(list(enumerate(ccp_alphas))):
        if accuracies[i] is None:
            continue
        accuracy = ccp_accuracy(i)
        # Train the final tree with the highest sampled CCP alpha which
        # produced the accuracy within 0.1% of the best accuracy
        if accuracy >= best_accuracy - 0.001:
            n_sampled = len([a for a in accuracies if a is not None])
            print(
                f" choosing ccp_alpha {ccp_alpha}, accuracy: {accuracy:.3f} (best: {best_accuracy:.3f}, orig: {accuracies[0]:.3f}) after searching {n_sampled}/{len(accuracies)} ccp_alphas"
            )
            #            print(f' searched {n_sampled} out of {len(accuracies)} ccp_alphas')
            return train_tree(X_train, y_train, ccp_alpha=ccp_alpha)

    assert False


#    return initial_tree


def predict_for_entry_point(entry_point, entry_df):
    num_predictions = entry_df.shape[0]

    # If every prediction is identical, we know we can predict it with
    # entry_point alone, so create a dataframe assuming the tree would've
    # correctly predicted it
    unique_predictions = entry_df.groupby(prediction_columns).ngroups
    if unique_predictions == 1:
        data = {c + "_accuracy": num_predictions for c in prediction_columns}
        data.update(
            {
                "correct": True,
                "overall_accuracy": num_predictions,
                "entry_point": num_predictions,
            }
        )
        usage_metrics = pd.DataFrame(data, index=[0])

    elif num_predictions < 4:
        # If we have fewer than 4 predictions, we can't split into our
        # desired train/test groupings, so for now just assume they were
        # all incorrectly predicted (probably not too far off since there
        # are at most 3 of them, and we know they don't all have the same
        # predictions)
        data = {c + "_accuracy": num_predictions for c in prediction_columns}
        data.update(
            {
                "correct": False,
                "overall_accuracy": 0,
                "entry_point": num_predictions,
            }
        )
        usage_metrics = pd.DataFrame(data, index=[0])
    else:
        # Pseudo-randomly split the samples into training (75%) and test (25%)
        df_train, df_test = train_test_split(entry_df, test_size=0.25, random_state=0)

        # Train the decision tree on the training set
        clf = find_best_tree(df_train, df_test)

        usage_metrics = get_metrics(clf, df_test)

    usage_metrics["entry_pc"] = entry_point
    usage_metrics[f"weighted_morph_{unique_predictions}"] = usage_metrics["entry_point"]

    return usage_metrics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", required=True, type=str, help="trace name (reported in resulting CSV)")
    parser.add_argument("--workload", default=None, type=str, help="workload name (reported in resulting CSV)")
    parser.add_argument("--suite", default=None, type=str, help="suite name (reported in resulting CSV)")
    parser.add_argument("--weight", default=None, type=str, help="trace weight (reported in resulting CSV)")
    parser.add_argument(
        "--input", type=argparse.FileType("r"), required=True, help="CSV file to classify (output of `is_branch_csv`)"
    )
    parser.add_argument("--global-history-depth", type=int, default=100, help="Global branch history depth to consider")
    parser.add_argument("--local-history-depth", type=int, default=10, help="Local branch history depth to consider")
    parser.add_argument(
        "--include-untaken-history", action="store_true", help="Include untaken branches in the branch history"
    )
    parser.add_argument("--detailed-output", type=str, help="path to write detailed output CSV file to")
    parser.add_argument("--summary-output", type=str, help="path to write summary output CSV file to")
    parser.add_argument(
        "--drop",
        action="append",
        default=[],
        help="Drop the supplied column (or columns if supplied more than once) before predicting",
    )
    args = parser.parse_args()

    # Add any columns specified on the command-line to the list of columns to drop
    global drop_columns
    drop_columns += args.drop

    # Pull in the branch data, including getting history situated
    df = get_branch_df(
        args.input.name, args.global_history_depth, args.local_history_depth, args.include_untaken_history
    )

    # Split by entry point, predict each entry point separately
    print("Splitting data by entry PC, training independent trees, and calculating results...")
    results = pd.DataFrame()

    # stats for progress tracking
    entry_groups = df.groupby("entry_point")
    n_entry_groups = entry_groups.ngroups
    completed_groups = 0

    for entry_point, entry_df in entry_groups:
        print(f"Progress: {round(completed_groups * 100.0 / n_entry_groups, 2)}%    ", end="\r", flush=True)
        completed_groups += 1

        usage_metrics = predict_for_entry_point(entry_point, entry_df)
        results = pd.concat([results, usage_metrics], ignore_index=True)
    print("\n", flush=True)  # get past '\r' so it doesn't eat the next thing printed

    results = results.reset_index(drop=True)

    print("Accuracy:")
    print(
        results[["branch_pc_accuracy", "taken_accuracy", "target_accuracy", "overall_accuracy"]].sum()
        / results["entry_point"].sum()
    )
    print("")

    print("Top 20 features by usage:")
    printable_results_columns = [
        c
        for c in results.columns.to_list()
        if "_accuracy" not in c
        and "max_depth" not in c
        and "weighted_morph" not in c
        and c not in ["entry_pc", "correct"]
        and c not in all_categories
    ]
    printable_results = (
        results[printable_results_columns].sum().to_frame("count").sort_values(by="count", ascending=False)
    )
    print(printable_results.head(20))
    print("")

    print("Top 20 categories by usage (note that they may overlap):")
    category_columns = [c for c in results.columns.to_list() if c in all_categories]
    printable_category_results = (
        results[category_columns].sum().to_frame("count").sort_values(by="count", ascending=False)
    )
    print(printable_category_results.head(20))
    print("")

    # Add other information to output dataframe
    def add_classifiers(res_df):
        res_df["trace"] = args.trace
        if args.workload:
            res_df["workload"] = args.workload
        if args.suite:
            res_df["suite"] = args.suite
        if args.weight:
            res_df["weight"] = args.weight
        return res_df

    summary_df = results[results["correct"] == True].sum().to_frame().T
    if args.summary_output:
        add_classifiers(summary_df).to_csv(args.summary_output)
    if args.detailed_output:
        add_classifiers(results).to_csv(args.detailed_output)


if __name__ == "__main__":
    main()
